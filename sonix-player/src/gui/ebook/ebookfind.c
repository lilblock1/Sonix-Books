#include "ebookfind.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/ebook/ebooksearch.h"
#include "src/system/core/utils.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/theme.h"

#define FIND_KEYBOARD_H 316

typedef struct {
	uint32_t generation;
	char path[800], query[EBOOKSEARCH_QUERY_BYTES];
} request_t;
typedef struct {
	uint32_t generation;
	ebooksearch_result_t result;
} result_t;

static gui_config_t *cfg;
static lv_obj_t *reader, *overlay, *field, *notice, *list;
static keyboard_t *keyboard;
static lv_timer_t *timer;
static void (*restore_cb)(ebook_position_t, bool);
static char book_path[800], searched[EBOOKSEARCH_QUERY_BYTES];
static ebook_position_t origin, destination;
static bool active, closing, selected, searching, started;
static int list_top;
static result_t *shown;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static request_t *pending;
static result_t *completed;
static bool running;
static _Atomic uint32_t generation, failed_generation, progress_chapter, progress_total;

static bool cancelled(void *user) {
	return ((request_t *)user)->generation != atomic_load(&generation);
}

static void progress(uint32_t chapter, uint32_t total, void *user) {
	if (cancelled(user)) return;
	atomic_store(&progress_total, total);
	atomic_store(&progress_chapter, chapter);
}

static void *worker(void *unused) {
	(void)unused;
	thread_be_background("ebookfind");
	for (;;) {
		pthread_mutex_lock(&lock);
		while (!pending) pthread_cond_wait(&wake, &lock);
		request_t *request = pending;
		pending = NULL;
		running = true;
		pthread_mutex_unlock(&lock);
		result_t *result = calloc(1, sizeof(*result));
		if (result) {
			result->generation = request->generation;
			ebooksearch_run(request->path, request->query, &result->result, cancelled, progress, request);
		} else atomic_store(&failed_generation, request->generation);
		/* ebooksearch_run closes its book before running becomes false. Closing
		 * the overlay waits asynchronously for this, before restoring a chapter. */
		pthread_mutex_lock(&lock);
		running = false;
		if (result && !cancelled(request)) {
			free(completed);
			completed = result;
			result = NULL;
		}
		pthread_mutex_unlock(&lock);
		free(result);
		free(request);
	}
	return NULL;
}

static void cancel_work(void) {
	atomic_fetch_add(&generation, 1);
	pthread_mutex_lock(&lock);
	free(pending); pending = NULL;
	free(completed); completed = NULL;
	pthread_mutex_unlock(&lock);
	atomic_store(&progress_chapter, 0);
	atomic_store(&progress_total, 0);
}

static void clear_results(void) {
	if (list) lv_obj_clean(list);
	free(shown); shown = NULL;
}

static void keyboard_show(bool show) {
	keyboard_set_visible(keyboard, show);
	int height = (int)cfg->screen_height - list_top - (show ? FIND_KEYBOARD_H : 0);
	lv_obj_set_height(list, height > 40 ? height : 40);
}

static void finish_close(void) {
	active = closing = searching = false;
	keyboard_show(false);
	lv_obj_set_hidden(overlay, true);
	lv_timer_pause(timer);
	if (restore_cb) restore_cb(destination, selected);
}

static void close_request(ebook_position_t target, bool picked) {
	if (closing) return;
	destination = target;
	selected = picked;
	closing = true;
	searching = false;
	cancel_work();
	clear_results();
	keyboard_show(false);
	lv_label_set_text(notice, "Returning to book...");
	pthread_mutex_lock(&lock);
	bool busy = running;
	pthread_mutex_unlock(&lock);
	if (!busy) finish_close();
	else lv_timer_resume(timer);
}

static void back_cb(lv_event_t *e) { (void)e; close_request(origin, false); }

static void hit_cb(lv_event_t *e) {
	int index = (int)(intptr_t)lv_event_get_user_data(e);
	if (!shown || closing || index < 0 || index >= shown->result.count) return;
	ebook_position_t target = shown->result.hits[index].position;
	close_request(target, true);
}

static void render_results(void) {
	ebooksearch_result_t *result = &shown->result;
	if (result->status == EBOOKSEARCH_EMPTY_QUERY) lv_label_set_text(notice, "Enter text to search.");
	else if (result->status == EBOOKSEARCH_LONG_QUERY) lv_label_set_text(notice, "The search phrase is too long.");
	else if (result->status == EBOOKSEARCH_OPEN_FAILED) lv_label_set_text(notice, "Could not open this book for search.");
	else if (result->truncated) lv_label_set_text_fmt(notice, "%d results; search limit reached.", result->count);
	else if (result->unreadable) lv_label_set_text_fmt(notice, "%d results; %u chapters could not be read.", result->count, result->unreadable);
	else if (!result->count) lv_label_set_text(notice, "No matches in this book.");
	else lv_label_set_text_fmt(notice, "%d result%s", result->count, result->count == 1 ? "" : "s");
	for (int i = 0; i < result->count; i++) {
		ebooksearch_hit_t *hit = &result->hits[i];
		lv_obj_t *row = lv_btn_create(list);
		lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
		lv_obj_add_style(row, &theme_style_card, 0);
		lv_obj_add_style(row, &theme_style_card_pressed, LV_STATE_PRESSED);
		lv_obj_set_style_pad_all(row, 14, 0);
		lv_obj_set_style_pad_row(row, 6, 0);
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_scrollable(row, false);
		lv_obj_add_event_cb(row, hit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
		lv_obj_t *chapter = lv_label_create(row);
		lv_label_set_text_fmt(chapter, "Chapter %u", hit->position.spine + 1);
		lv_obj_add_style(chapter, &theme_style_text_dim, 0);
		lv_obj_set_style_text_font(chapter, &font_ui_18, 0);
		lv_obj_t *text = lv_label_create(row);
		lv_obj_set_size(text, lv_pct(100), 72);
		lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
		lv_label_set_text(text, hit->snippet);
		lv_obj_add_style(text, &theme_style_text, 0);
		lv_obj_set_style_text_font(text, &font_ui_20, 0);
	}
	lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
}

static void poll_cb(lv_timer_t *t) {
	if (!active) { lv_timer_pause(t); return; }
	pthread_mutex_lock(&lock);
	bool busy = running;
	result_t *result = completed;
	completed = NULL;
	pthread_mutex_unlock(&lock);
	if (closing) {
		free(result);
		if (!busy) finish_close();
		return;
	}
	if (!searching) { free(result); lv_timer_pause(t); return; }
	if (!result) {
		if (atomic_load(&failed_generation) == atomic_load(&generation)) {
			lv_label_set_text(notice, "Not enough memory to search.");
			searching = false;
			lv_timer_pause(t);
		} else {
			uint32_t chapter = atomic_load(&progress_chapter), total = atomic_load(&progress_total);
			if (chapter && total) lv_label_set_text_fmt(notice, "Searching chapter %u of %u...", chapter, total);
		}
		return;
	}
	if (result->generation != atomic_load(&generation)) { free(result); return; }
	shown = result;
	searching = false;
	lv_timer_pause(t);
	render_results();
}

static void search_cb(lv_event_t *e) {
	(void)e;
	if (!active || closing) return;
	searching = false;
	lv_timer_pause(timer);
	cancel_work();
	clear_results();
	snprintf(searched, sizeof(searched), "%s", lv_textarea_get_text(field));
	if (!searched[0]) { lv_label_set_text(notice, "Enter text to search."); return; }
	if (!started) {
		pthread_t thread;
		if (pthread_create(&thread, NULL, worker, NULL) != 0) {
			lv_label_set_text(notice, "Could not start book search. Try reopening Find."); return;
		}
		pthread_detach(thread);
		started = true;
	}
	request_t *request = calloc(1, sizeof(*request));
	if (!request) { lv_label_set_text(notice, "Not enough memory to search."); return; }
	request->generation = atomic_load(&generation);
	snprintf(request->path, sizeof(request->path), "%s", book_path);
	snprintf(request->query, sizeof(request->query), "%s", searched);
	pthread_mutex_lock(&lock);
	pending = request;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
	searching = true;
	keyboard_show(false);
	lv_label_set_text(notice, "Searching this book...");
	lv_timer_resume(timer);
}

static void field_clicked_cb(lv_event_t *e) { (void)e; if (!closing) keyboard_show(true); }

static void field_changed_cb(lv_event_t *e) {
	(void)e;
	if (!active || closing) return;
	if (strcmp(lv_textarea_get_text(field), searched)) {
		cancel_work(); clear_results(); searching = false;
		lv_timer_pause(timer);
		lv_label_set_text(notice, "Press Search to find text in this book.");
	}
}

static void clear_cb(lv_event_t *e) {
	(void)e;
	if (closing) return;
	lv_textarea_set_text(field, "");
	keyboard_show(true);
}

static lv_obj_t *button(const char *text, int x, int y, int width, lv_event_cb_t cb) {
	lv_obj_t *obj = lv_btn_create(overlay);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, width, 56);
	lv_obj_add_style(obj, &theme_style_card, 0);
	lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *label = lv_label_create(obj);
	lv_label_set_text(label, text);
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_obj_set_style_text_font(label, &font_ui_20, 0);
	lv_obj_center(label);
	return obj;
}

static void build(void) {
	int pad = cfg->padding, width = cfg->screen_width;
	overlay = lv_obj_create(reader);
	lv_obj_remove_style_all(overlay);
	lv_obj_add_style(overlay, &theme_style_screen, 0);
	lv_obj_set_size(overlay, width, cfg->screen_height);
	lv_obj_set_scrollable(overlay, false);
	button("Back", pad, pad, 64, back_cb);
	lv_obj_t *title = lv_label_create(overlay);
	lv_label_set_text(title, "Find in book");
	lv_obj_add_style(title, &theme_style_text, 0);
	lv_obj_set_style_text_font(title, &font_ui_24, 0);
	lv_obj_set_pos(title, pad + 76, pad + 12);
	int top = pad + 68;
	field = lv_textarea_create(overlay);
	lv_textarea_set_one_line(field, true);
	lv_textarea_set_max_length(field, 60);
	lv_textarea_set_placeholder_text(field, "Find text in this book");
	lv_obj_set_pos(field, pad, top);
	lv_obj_set_size(field, width - 2 * pad - 70, 60);
	lv_obj_add_style(field, &theme_style_card, 0);
	lv_obj_set_style_text_font(field, &font_ui_20, 0);
	keyboard_style_caret(field);
	lv_obj_add_event_cb(field, field_clicked_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(field, field_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
	button("Clear", width - pad - 64, top + 2, 64, clear_cb);
	notice = lv_label_create(overlay);
	lv_obj_set_pos(notice, pad, top + 68);
	lv_obj_set_size(notice, width - 2 * pad, 44);
	lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
	lv_obj_add_style(notice, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(notice, &font_ui_18, 0);
	list_top = top + 116;
	list = lv_obj_create(overlay);
	lv_obj_remove_style_all(list);
	lv_obj_set_pos(list, pad, list_top);
	lv_obj_set_width(list, width - 2 * pad);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(list, 8, 0);
	keyboard = keyboard_create(overlay, width, FIND_KEYBOARD_H, field, &icon_search, NULL, search_cb, NULL);
}

void ebookfind_init(gui_config_t *config, lv_obj_t *parent, void (*restore)(ebook_position_t, bool)) {
	cfg = config; reader = parent; restore_cb = restore;
	timer = lv_timer_create(poll_cb, 100, NULL);
	lv_timer_pause(timer);
}

void ebookfind_open(const char *path, ebook_position_t position) {
	if (!overlay) build();
	cancel_work(); clear_results();
	origin = position;
	active = true; closing = searching = selected = false;
	snprintf(book_path, sizeof(book_path), "%s", path);
	searched[0] = 0;
	lv_textarea_set_text(field, "");
	lv_label_set_text(notice, "Press Search to find text in this book.");
	keyboard_reset(keyboard);
	keyboard_show(true);
	lv_obj_set_hidden(overlay, false);
	lv_obj_move_foreground(overlay);
}

bool ebookfind_active(void) { return active; }
bool ebookfind_key(gui_key_t key) {
	if (!active) return false;
	if (key == GUI_KEY_PLAY_PAUSE && !closing) close_request(origin, false);
	return true;
}

void ebookfind_abort(void) {
	active = closing = searching = false;
	cancel_work(); clear_results();
	if (overlay) { keyboard_show(false); lv_obj_set_hidden(overlay, true); }
	if (timer) lv_timer_pause(timer);
}
