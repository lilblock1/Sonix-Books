#include "ebookpage.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "src/system/ebook/ebooklibrary.h"
#include "src/system/ebook/ebookhistory.h"
#include "src/system/ebook/ebookprogress.h"
#include "src/system/ebook/bookmarks.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/core/utils.h"
#include "src/gui/nowplaying/cover.h"
#include "src/gui/ebook/ebookcovers.h"
#include "src/gui/ebook/ebookmarkspage.h"
#include "src/gui/ebook/ebookreader.h"
#include "src/gui/ebook/ebooksettings.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/popover.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"

lv_obj_t *ebookpage_screen;

#define TILE_GAP 14
#define COVER_LOOKAHEAD 400
#define KEYBOARD_HEIGHT 316

/* Metadata is owned by library; each tile only owns its decoded picture. */
typedef struct {
	lv_obj_t *tile, *glyph, *image;
	cover_image_t cover;
	bool has_cover;
} shelf_entry_t;

static gui_config_t *g_cfg;
static lv_obj_t *grid, *page_container, *empty_note, *resume_row;
static lv_obj_t *field, *breadcrumb, *up_button, *sort_button;
static keyboard_t *keyboard;
static lv_timer_t *scan_timer;
static ebooklibrary_result_t *library;
static shelf_entry_t *shelf;
static int resume_index = -1;
static int cover_w, cover_h, content_top;
static char root[EBOOKLIBRARY_PATH], folder[EBOOKLIBRARY_PATH], query[256];
static ebooklibrary_sort_t sort_order;
static const char *const sort_names[] = {"Title A-Z", "Title Z-A", "Author", "Newest", "Oldest"};
static uint32_t covers_token;
static bool active;

/* One worker for this page, with a replaceable pending request. Rapid queries
 * cancel at the next file, rather than creating a new thread for each tap.
 * LVGL and tile ownership stay exclusively on the UI thread. */
typedef struct {
	uint32_t generation;
	char root[EBOOKLIBRARY_PATH], folder[EBOOKLIBRARY_PATH], query[256], cache[1024], last_key[24];
	ebooklibrary_sort_t sort;
	bool continue_book;
} scan_request_t;

typedef struct {
	uint32_t generation;
	ebooklibrary_result_t *books;
	char continue_path[EBOOKLIBRARY_PATH];
	bool continue_book;
} scan_result_t;

static pthread_mutex_t scan_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scan_wake = PTHREAD_COND_INITIALIZER;
static _Atomic uint32_t scan_generation, failed_generation;
static scan_request_t *pending_scan;
static scan_result_t *completed_scan;
static bool scan_worker_started;

static void result_free(scan_result_t *r) {
	if (r) { free(r->books); free(r); }
}

static bool scan_cancelled(void *user) {
	return *(const uint32_t *)user != atomic_load(&scan_generation);
}

static bool continue_match(const char *path, void *user) {
	scan_request_t *request = user;
	char key[24];
	bookmark_key(path, key, sizeof(key));
	return request->last_key[0] && !strcmp(key, request->last_key);
}

static void *scan_worker(void *unused) {
	(void)unused;
	thread_be_background("ebooklibrary");
	for (;;) {
		pthread_mutex_lock(&scan_lock);
		while (!pending_scan) pthread_cond_wait(&scan_wake, &scan_lock);
		scan_request_t *request = pending_scan;
		pending_scan = NULL;
		pthread_mutex_unlock(&scan_lock);
		scan_result_t *result = calloc(1, sizeof(*result));
		if (result) {
			result->generation = request->generation;
			result->continue_book = request->continue_book;
			if (request->continue_book && request->last_key[0]) {
				ebooklibrary_find(request->root, continue_match, request, result->continue_path, sizeof(result->continue_path), scan_cancelled);
			} else if (!request->continue_book) {
				/* Cache failures (including a read-only SD card) are harmless. */
				char parent[1024];
				snprintf(parent, sizeof(parent), "%s", request->cache);
				char *slash = strrchr(parent, '/');
				if (slash) { *slash = 0; mkdir(parent, 0755); mkdir(request->cache, 0755); }
				result->books = malloc(sizeof(*result->books));
				if (result->books) ebooklibrary_scan(request->root, request->folder, request->query,
					request->sort, request->cache, result->books, scan_cancelled, &request->generation);
			}
			pthread_mutex_lock(&scan_lock);
			if (!scan_cancelled(&request->generation)) {
				result_free(completed_scan);
				completed_scan = result;
				result = NULL;
			}
			pthread_mutex_unlock(&scan_lock);
			result_free(result);
		} else atomic_store(&failed_generation, request->generation);
		free(request);
	}
	return NULL;
}

static void scan_cancel(void) {
	atomic_fetch_add(&scan_generation, 1);
	pthread_mutex_lock(&scan_lock);
	free(pending_scan);
	pending_scan = NULL;
	result_free(completed_scan);
	completed_scan = NULL;
	pthread_mutex_unlock(&scan_lock);
}

static void shelf_free(void) {
	ebookcovers_stop(covers_token);
	covers_token = 0;
	shelf_entry_t *old_shelf = shelf;
	ebooklibrary_result_t *old_library = library;
	shelf = NULL; library = NULL;
	lv_obj_clean(grid); /* disconnect LVGL's references before freeing pixels */
	if (old_shelf && old_library) for (int i = 0; i < old_library->count; i++) {
		if (old_shelf[i].has_cover) cover_free(&old_shelf[i].cover);
	}
	free(old_shelf);
	free(old_library);
	resume_index = -1;
	lv_obj_set_hidden(resume_row, true);
}

static bool near_view(int index) {
	if (!shelf || !library || index < 0 || index >= library->count || !shelf[index].tile) return false;
	lv_area_t area, visible;
	lv_obj_get_coords(page_container, &visible);
	lv_obj_get_coords(shelf[index].tile, &area);
	return area.y2 >= visible.y1 - COVER_LOOKAHEAD && area.y1 <= visible.y2 + COVER_LOOKAHEAD;
}

static bool cover_ready(int index, const char *title, const cover_image_t *cover) {
	(void)title; /* The metadata scan already established the title and ordering. */
	if (!shelf || !library || index < 0 || index >= library->count || !shelf[index].image) return false;
	if (!near_view(index)) {
		ebookcovers_forget(covers_token, index);
		return false;
	}
	if (!cover) return false;
	shelf_entry_t *entry = &shelf[index];
	if (entry->has_cover) { lv_image_set_src(entry->image, NULL); cover_free(&entry->cover); }
	entry->cover = *cover;
	entry->has_cover = true;
	lv_image_set_src(entry->image, &entry->cover.dsc);
	lv_obj_set_hidden(entry->image, false);
	return true;
}

static void want_visible_covers(void) {
	if (!shelf || !library) return;
	lv_obj_update_layout(page_container);
	/* Never keep the entire shelf decoded on the R1's 64 MB RAM. */
	int first = -1, last = -1;
	for (int i = 0; i < library->count; i++) {
		if (near_view(i)) {
			if (!library->entries[i].directory) { if (first < 0) first = i; last = i; }
		} else if (shelf[i].has_cover) {
			lv_obj_set_hidden(shelf[i].image, true);
			lv_image_set_src(shelf[i].image, NULL);
			cover_free(&shelf[i].cover);
			shelf[i].has_cover = false;
			ebookcovers_forget(covers_token, i);
		}
	}
	/* An empty range must also replace an old request. */
	ebookcovers_want(covers_token, first < 0 ? 0 : first, last);
}

static void scrolled_cb(lv_event_t *e) { (void)e; want_visible_covers(); }
static void scan_begin(bool continue_book);

static void keyboard_show(bool show) {
	keyboard_set_visible(keyboard, show);
	int height = (int)g_cfg->screen_height - content_top - (show ? KEYBOARD_HEIGHT : 0);
	lv_obj_set_height(page_container, height > 40 ? height : 40);
	want_visible_covers();
}

static void open_book_cb(lv_event_t *e) {
	if (player_sheet_drag_active() || switcher_back_drag_active()) return;
	int index = (int)(intptr_t)lv_event_get_user_data(e);
	if (!library || index < 0 || index >= library->count) return;
	if (!gui_card_available()) { gui_notify_no_card(); return; }
	ebooklibrary_entry_t *entry = &library->entries[index];
	if (entry->directory) {
		size_t n = strlen(root);
		if (strncmp(entry->path, root, n) || entry->path[n] != '/') return;
		snprintf(folder, sizeof(folder), "%s", entry->path + n + 1);
		query[0] = 0;
		lv_textarea_set_text(field, "");
		keyboard_show(false);
		scan_begin(false);
	} else {
		/* Opening unloads the shelf synchronously, so do not pass its memory. */
		char book_path[EBOOKLIBRARY_PATH];
		snprintf(book_path, sizeof(book_path), "%s", entry->path);
		ebookreader_open(book_path);
	}
}

static void progress_badge(lv_obj_t *plate, const char *book_path) {
	ebookprogress_t progress = ebookprogress_get(book_path);
	lv_obj_t *badge = lv_label_create(plate);
	if (progress.state == EBOOKPROGRESS_NEW) lv_label_set_text(badge, "New");
	else if (progress.state == EBOOKPROGRESS_FINISHED) lv_label_set_text(badge, "Finished 100%");
	else if (progress.known) lv_label_set_text_fmt(badge, "Reading %d%%", progress.percent);
	else lv_label_set_text(badge, "Reading");
	lv_obj_set_style_text_font(badge, &font_ui_18, 0);
	lv_obj_set_style_text_color(badge, lv_color_white(), 0);
	lv_obj_set_style_bg_color(badge, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(badge, LV_OPA_80, 0);
	lv_obj_set_style_pad_hor(badge, 6, 0);
	lv_obj_set_style_pad_ver(badge, 3, 0);
	lv_obj_set_style_radius(badge, 5, 0);
	lv_obj_align(badge, LV_ALIGN_BOTTOM_MID, 0, -18);
	lv_obj_set_event_bubble(badge, true);

	/* Two small rectangles reuse existing LVGL objects; no bar widget, timer,
	 * extra images, or chapter parsing is needed by the shelf. */
	lv_obj_t *track = lv_obj_create(plate);
	lv_obj_remove_style_all(track);
	lv_obj_set_size(track, cover_w - 16, 6);
	lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -7);
	lv_obj_set_style_bg_color(track, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(track, LV_OPA_80, 0);
	lv_obj_set_style_radius(track, 3, 0);
	lv_obj_set_clickable(track, false);
	lv_obj_set_scrollable(track, false);
	lv_obj_t *fill = lv_obj_create(track);
	lv_obj_remove_style_all(fill);
	lv_obj_set_size(fill, lv_pct(progress.percent), 6);
	lv_obj_set_style_bg_color(fill, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(fill, 3, 0);
	lv_obj_set_clickable(fill, false);
	lv_obj_set_scrollable(fill, false);
	/* A legacy position has no measured percentage until the book is opened. */
	lv_obj_set_hidden(track, !progress.known);
}

static void build_tiles(void) {
	int width = (int)g_cfg->screen_width - 2 * g_cfg->padding;
	cover_w = (width - TILE_GAP) / 2;
	cover_h = cover_w * 3 / 2;
	shelf = calloc((size_t)library->count, sizeof(*shelf));
	if (!shelf) {
		lv_label_set_text(empty_note, "Not enough memory to show books.");
		lv_obj_set_hidden(empty_note, false);
		return;
	}
	for (int i = 0; i < library->count; i++) {
		ebooklibrary_entry_t *book = &library->entries[i];
		shelf_entry_t *entry = &shelf[i];
		lv_obj_t *tile = lv_obj_create(grid);
		lv_obj_remove_style_all(tile);
		lv_obj_set_size(tile, cover_w, cover_h + 74);
		lv_obj_set_scrollable(tile, false);
		lv_obj_set_clickable(tile, true);
		lv_obj_set_event_bubble(tile, true);
		lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_style_pad_row(tile, 6, 0);
		lv_obj_add_event_cb(tile, open_book_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
		entry->tile = tile;

		lv_obj_t *plate = lv_obj_create(tile);
		lv_obj_remove_style_all(plate);
		lv_obj_set_size(plate, cover_w, cover_h);
		lv_obj_add_style(plate, &theme_style_card, 0);
		lv_obj_set_style_radius(plate, 8, 0);
		lv_obj_set_style_border_width(plate, 0, 0);
		lv_obj_set_style_shadow_width(plate, 0, 0);
		lv_obj_set_scrollable(plate, false);
		lv_obj_set_event_bubble(plate, true);
		entry->glyph = lv_image_create(plate);
		lv_image_set_src(entry->glyph, book->directory ? &icon_folder : &icon_ebook_cover);
		lv_obj_center(entry->glyph);
		lv_obj_set_style_image_recolor(entry->glyph, theme()->text_secondary, 0);
		lv_obj_set_style_image_recolor_opa(entry->glyph, LV_OPA_COVER, 0);
		lv_obj_set_style_image_opa(entry->glyph, LV_OPA_50, 0);
		if (book->directory) {
			lv_obj_t *hint = lv_label_create(plate);
			lv_label_set_text(hint, "Folder");
			lv_obj_add_style(hint, &theme_style_text, 0);
			lv_obj_set_style_text_font(hint, &font_ui_20, 0);
			lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
			lv_obj_set_event_bubble(hint, true);
		} else {
			entry->image = lv_image_create(plate);
			lv_obj_center(entry->image);
			lv_obj_set_hidden(entry->image, true);
			lv_obj_set_event_bubble(entry->image, true);
			progress_badge(plate, book->path);
		}
		lv_obj_t *label = lv_label_create(tile);
		lv_obj_set_size(label, cover_w, 42);
		lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
		lv_label_set_text(label, book->title);
		lv_obj_add_style(label, &theme_style_text, 0);
		lv_obj_set_style_text_font(label, &font_ui_18, 0);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_event_bubble(label, true);
		lv_obj_t *author = lv_label_create(tile);
		lv_obj_set_size(author, cover_w, 24);
		lv_label_set_long_mode(author, LV_LABEL_LONG_DOT);
		lv_label_set_text(author, book->directory ? "Open folder" : book->author);
		lv_obj_add_style(author, &theme_style_text_dim, 0);
		lv_obj_set_style_text_font(author, &font_ui_18, 0);
		lv_obj_set_style_text_align(author, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_event_bubble(author, true);
		if (!book->directory && ebookhistory_is_last(book->path)) resume_index = i;
	}
	if (resume_index >= 0) {
		char text[300];
		snprintf(text, sizeof(text), "Continue: %s", library->entries[resume_index].title);
		lv_label_set_text(settingsrow_name_label(resume_row), text);
		lv_obj_set_hidden(resume_row, false);
	}
	char (*paths)[800] = calloc((size_t)library->count, sizeof(*paths));
	if (paths) {
		for (int i = 0; i < library->count; i++) if (!library->entries[i].directory)
			snprintf(paths[i], sizeof(paths[i]), "%s", library->entries[i].path);
		covers_token = ebookcovers_start_paths((const char (*)[800])paths, library->count, cover_w, cover_h, cover_ready);
		free(paths);
		want_visible_covers();
	}
}

static void scan_done_cb(lv_timer_t *timer) {
	pthread_mutex_lock(&scan_lock);
	scan_result_t *result = completed_scan;
	completed_scan = NULL;
	pthread_mutex_unlock(&scan_lock);
	if (!result) {
		if (active && atomic_load(&failed_generation) == atomic_load(&scan_generation)) {
			lv_label_set_text(empty_note, "Not enough memory to scan books.");
			lv_timer_pause(timer);
		}
		return;
	}
	if (!active || result->generation != atomic_load(&scan_generation)) { result_free(result); return; }
	lv_timer_pause(timer);
	if (result->continue_book) {
		char book_path[EBOOKLIBRARY_PATH];
		snprintf(book_path, sizeof(book_path), "%s", result->continue_path);
		result_free(result);
		if (book_path[0] && gui_card_available()) {
			ebookreader_open(book_path);
			if (active) scan_begin(false); /* unreadable book: return to a usable shelf */
		}
		else scan_begin(false);
		return;
	}
	library = result->books;
	result->books = NULL;
	result_free(result);
	if (!gui_card_available()) {
		shelf_free();
		lv_label_set_text(empty_note, tr("books_card_unavailable"));
		return;
	}
	if (!library) { lv_label_set_text(empty_note, "Not enough memory to read the library."); return; }
	if (library->unavailable) lv_label_set_text(empty_note, "Folder unavailable. Check the SD card or use Up.");
	else if (!library->count) lv_label_set_text(empty_note, library->truncated ?
		"Search limit reached. Browse a subfolder to find books beyond the limit." :
		(query[0] ? "No matching books in Ebook or its subfolders." : "No books here. Copy EPUB books or folders into Ebook on the SD card."));
	else {
		lv_obj_set_hidden(empty_note, !library->truncated && !library->unreadable);
		lv_label_set_text(empty_note, library->truncated ? "Library limit reached. Showing up to 300 results; browse a subfolder or narrow your search." : "Some folders could not be read.");
		build_tiles();
	}
}

static void refresh_nav(void) {
	char text[900];
	snprintf(text, sizeof(text), "%s%s", query[0] ? "Search all Ebook folders" : "Ebook", !query[0] && folder[0] ? "/" : "");
	if (!query[0] && folder[0]) strncat(text, folder, sizeof(text) - strlen(text) - 1);
	lv_label_set_text(breadcrumb, text);
	lv_obj_set_hidden(up_button, !query[0] && !folder[0]);
}

static void scan_begin(bool continue_book) {
	scan_cancel();
	lv_timer_pause(scan_timer);
	shelf_free();
	refresh_nav();
	lv_obj_scroll_to_y(page_container, 0, LV_ANIM_OFF);
	lv_obj_set_hidden(empty_note, false);
	if (!gui_card_available()) { lv_label_set_text(empty_note, tr("books_card_unavailable")); return; }
	lv_label_set_text(empty_note, continue_book ? "Finding your last book..." : "Reading book titles and authors...");
	if (!scan_worker_started) {
		pthread_t worker;
		if (pthread_create(&worker, NULL, scan_worker, NULL) != 0) {
			lv_label_set_text(empty_note, "Could not start the library scan. Reopen Books to try again.");
			return;
		}
		pthread_detach(worker);
		scan_worker_started = true;
	}
	scan_request_t *request = calloc(1, sizeof(*request));
	if (!request) { lv_label_set_text(empty_note, "Not enough memory to scan books."); return; }
	request->generation = atomic_load(&scan_generation);
	request->sort = sort_order;
	request->continue_book = continue_book;
	snprintf(request->last_key, sizeof(request->last_key), "%s", config_store_get(config_ebook_store(), "ebook", "last_book", ""));
	snprintf(request->root, sizeof(request->root), "%s", root);
	snprintf(request->folder, sizeof(request->folder), "%s", folder);
	snprintf(request->query, sizeof(request->query), "%s", query);
	snprintf(request->cache, sizeof(request->cache), "%s/.local/ebook-meta", g_cfg->sd_root_path ? g_cfg->sd_root_path : "");
	pthread_mutex_lock(&scan_lock);
	pending_scan = request;
	pthread_cond_signal(&scan_wake);
	pthread_mutex_unlock(&scan_lock);
	lv_timer_resume(scan_timer);
}

static void search_cb(lv_event_t *e) {
	(void)e;
	snprintf(query, sizeof(query), "%s", lv_textarea_get_text(field));
	/* Whitespace-only searches are the normal folder view. */
	char *p = query;
	while (*p == ' ' || *p == '\t') p++;
	memmove(query, p, strlen(p) + 1);
	size_t n = strlen(query);
	while (n && (query[n - 1] == ' ' || query[n - 1] == '\t')) query[--n] = 0;
	lv_textarea_set_text(field, query);
	keyboard_show(false);
	scan_begin(false);
}

static void field_cb(lv_event_t *e) { (void)e; keyboard_show(true); }
static void clear_cb(lv_event_t *e) {
	(void)e; query[0] = 0; lv_textarea_set_text(field, ""); keyboard_show(false); scan_begin(false);
}
static void up_cb(lv_event_t *e) {
	(void)e;
	if (player_sheet_drag_active() || switcher_back_drag_active()) return;
	if (query[0]) { query[0] = 0; lv_textarea_set_text(field, ""); }
	else { char *slash = strrchr(folder, '/'); if (slash) *slash = 0; else folder[0] = 0; }
	keyboard_show(false);
	scan_begin(false);
}
static void sort_choose(void *user) {
	sort_order = (ebooklibrary_sort_t)(intptr_t)user;
	config_store_set_int(config_ebook_store(), "ebook", "library_sort", sort_order);
	config_store_save(config_ebook_store());
	lv_label_set_text(lv_obj_get_child(sort_button, 0), sort_names[sort_order]);
	keyboard_show(false);
	scan_begin(false);
}
static void sort_cb(lv_event_t *e) {
	(void)e;
	if (player_sheet_drag_active() || switcher_back_drag_active()) return;
	popover_item_t items[EBOOKLIBRARY_SORT_COUNT];
	for (int i = 0; i < EBOOKLIBRARY_SORT_COUNT; i++) items[i] = (popover_item_t){
		.label = sort_names[i], .action = sort_choose, .user = (void *)(intptr_t)i, .checked = (int)sort_order == i};
	popover_show(sort_button, items, EBOOKLIBRARY_SORT_COUNT);
}
static void resume_cb(lv_event_t *e) {
	(void)e;
	if (resume_index < 0) return;
	/* open_book_cb copies the path before changing screens. */
	if (player_sheet_drag_active() || switcher_back_drag_active() || !library) return;
	if (!gui_card_available()) { gui_notify_no_card(); return; }
	char book_path[EBOOKLIBRARY_PATH];
	snprintf(book_path, sizeof(book_path), "%s", library->entries[resume_index].path);
	ebookreader_open(book_path);
}
static void settings_cb(lv_event_t *e) {
	(void)e; if (!player_sheet_drag_active() && !switcher_back_drag_active()) switch_screen(ebooksettings_screen);
}
static void bookmarks_cb(lv_event_t *e) {
	(void)e; if (!player_sheet_drag_active() && !switcher_back_drag_active()) ebookmarkspage_open();
}

void ebookpage_open(void) {
	if (!gui_card_available()) { gui_notify_no_card(); return; }
	switch_screen(ebookpage_screen);
}
void ebookpage_continue(void) {
	if (!gui_card_available()) { gui_notify_no_card(); return; }
	switch_screen(ebookpage_screen);
	/* Resolve nested paths off the UI thread, without parsing books. */
	scan_begin(true);
}
static void loaded_cb(lv_event_t *e) {
	(void)e; active = true;
	int n = snprintf(root, sizeof(root), "%s/Ebook", g_cfg->sd_root_path ? g_cfg->sd_root_path : "");
	if (n < 0 || (size_t)n >= sizeof(root)) root[0] = 0;
	keyboard_show(false);
	scan_begin(false);
}
static void unloaded_cb(lv_event_t *e) {
	(void)e; active = false; scan_cancel(); lv_timer_pause(scan_timer); popover_close(); keyboard_show(false); shelf_free();
}
static void shelf_theme_refresh(void) {
	if (!shelf || !library) return;
	for (int i = 0; i < library->count; i++) if (shelf[i].glyph)
		lv_obj_set_style_image_recolor(shelf[i].glyph, theme()->text_secondary, 0);
}

static lv_obj_t *text_button(lv_obj_t *parent, const char *text, int width, lv_event_cb_t cb) {
	lv_obj_t *button = lv_btn_create(parent);
	lv_obj_set_size(button, width, 48);
	lv_obj_add_style(button, &theme_style_card, 0);
	lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *label = lv_label_create(button);
	lv_label_set_text(label, text);
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_obj_set_style_text_font(label, &font_ui_18, 0);
	lv_obj_center(label);
	return button;
}

void ebookpage_init(gui_config_t *cfg) {
	g_cfg = cfg;
	ebookpage_screen = lv_obj_create(NULL);
	lv_obj_add_style(ebookpage_screen, &theme_style_screen, 0);
	page_container = settingsrow_page(ebookpage_screen, cfg, "books");
	settingsrow_title_corner_slots(settingsrow_page_title(ebookpage_screen), cfg, 2);
	for (int i = 0; i < 2; i++) {
		lv_obj_t *button = lv_btn_create(ebookpage_screen);
		lv_obj_set_size(button, 56, 56);
		lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(button, 0, 0);
		lv_obj_set_style_shadow_width(button, 0, 0);
		lv_obj_set_style_pad_all(button, 0, 0);
		lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -cfg->padding - i * 62, cfg->padding + cfg->top_bar_height);
		lv_obj_add_event_cb(button, i ? bookmarks_cb : settings_cb, LV_EVENT_CLICKED, NULL);
		lv_obj_t *icon = lv_image_create(button);
		lv_image_set_src(icon, i ? &icon_bookmark : &icon_music_settings);
		lv_obj_add_style(icon, &theme_style_icon, 0);
		lv_obj_center(icon);
	}
	int top = settingsrow_content_top(cfg), width = cfg->screen_width - 2 * cfg->padding;
	field = lv_textarea_create(ebookpage_screen);
	lv_textarea_set_one_line(field, true);
	lv_textarea_set_max_length(field, 60); /* fits query buffer even with 4-byte code points */
	lv_textarea_set_placeholder_text(field, "Title or author (all folders)");
	lv_obj_set_size(field, width - 66, 62);
	lv_obj_set_pos(field, cfg->padding, top);
	lv_obj_add_style(field, &theme_style_card, 0);
	lv_obj_set_style_text_font(field, &font_ui_18, 0);
	keyboard_style_caret(field);
	lv_obj_add_event_cb(field, field_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *clear = text_button(ebookpage_screen, "Clear", 60, clear_cb);
	lv_obj_set_pos(clear, cfg->screen_width - cfg->padding - 60, top + 7);
	up_button = text_button(ebookpage_screen, "Up", 58, up_cb);
	lv_obj_set_pos(up_button, cfg->padding, top + 70);
	breadcrumb = lv_label_create(ebookpage_screen);
	lv_obj_set_pos(breadcrumb, cfg->padding + 66, top + 82);
	lv_obj_set_size(breadcrumb, width - 66 - 146, 24);
	lv_label_set_long_mode(breadcrumb, LV_LABEL_LONG_DOT);
	lv_obj_add_style(breadcrumb, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(breadcrumb, &font_ui_18, 0);
	sort_order = config_store_get_int(config_ebook_store(), "ebook", "library_sort", EBOOKLIBRARY_TITLE);
	if (sort_order < 0 || sort_order >= EBOOKLIBRARY_SORT_COUNT) sort_order = EBOOKLIBRARY_TITLE;
	sort_button = text_button(ebookpage_screen, sort_names[sort_order], 140, sort_cb);
	lv_obj_set_pos(sort_button, cfg->screen_width - cfg->padding - 140, top + 70);
	content_top = top + 126;
	lv_obj_set_y(page_container, content_top);
	lv_obj_set_height(page_container, cfg->screen_height - content_top);
	lv_obj_set_style_pad_row(page_container, TILE_GAP, 0);
	lv_obj_add_event_cb(page_container, scrolled_cb, LV_EVENT_SCROLL, NULL);
	lv_obj_add_event_cb(page_container, scrolled_cb, LV_EVENT_SCROLL_END, NULL);
	resume_row = settingsrow_action(page_container, "", resume_cb, NULL);
	settingsrow_name_lines(resume_row, 1);
	lv_obj_set_height(resume_row, 60);
	lv_obj_set_hidden(resume_row, true);
	empty_note = lv_label_create(page_container);
	lv_obj_set_width(empty_note, lv_pct(100));
	lv_label_set_long_mode(empty_note, LV_LABEL_LONG_WRAP);
	lv_obj_add_style(empty_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(empty_note, &font_ui_20, 0);
	grid = lv_obj_create(page_container);
	lv_obj_remove_style_all(grid);
	lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_style_pad_column(grid, TILE_GAP, 0);
	lv_obj_set_style_pad_row(grid, TILE_GAP, 0);
	lv_obj_set_scrollable(grid, false);
	lv_obj_set_event_bubble(grid, true);
	keyboard = keyboard_create(ebookpage_screen, cfg->screen_width, KEYBOARD_HEIGHT, field, &icon_search, NULL, search_cb, NULL);
	keyboard_set_visible(keyboard, false);
	scan_timer = lv_timer_create(scan_done_cb, 100, NULL);
	lv_timer_pause(scan_timer);
	lv_obj_add_event_cb(ebookpage_screen, loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
	lv_obj_add_event_cb(ebookpage_screen, unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
	switcher_attach_back_gesture(ebookpage_screen);
	switcher_attach_back_gesture(page_container);
	player_sheet_attach_drag(page_container, true);
	theme_register_refresh(shelf_theme_refresh);
}
