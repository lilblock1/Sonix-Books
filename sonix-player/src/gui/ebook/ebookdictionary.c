#include "ebookdictionary.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/ebook/ebookdict.h"
#include "src/system/core/utils.h"
#include "src/system/input/readingkeys.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/theme.h"

#define KEYBOARD_H 316

typedef struct {
	uint32_t generation;
	bool scan;
	char root[800], id[EBOOKDICT_ID], query[EBOOKDICT_QUERY];
} request_t;
typedef struct {
	uint32_t generation;
	bool scan;
	ebookdict_catalog_t catalog;
	ebookdict_result_t definition;
} result_t;

static gui_config_t *cfg;
static lv_obj_t *reader, *overlay, *field, *picker, *notice, *body, *meaning;
static keyboard_t *keyboard;
static lv_timer_t *timer;
static ebookdict_catalog_t catalog;
static char selected_id[EBOOKDICT_ID];
static bool active, loading, started, populating;
static int body_top;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static request_t *pending;
static result_t *completed;
static _Atomic uint32_t generation, failed_generation;

static bool cancelled(void *user) { return ((request_t *)user)->generation != atomic_load(&generation); }

static void *worker(void *unused) {
	(void)unused;
	thread_be_background("dictionary");
	for (;;) {
		pthread_mutex_lock(&lock);
		while (!pending) pthread_cond_wait(&wake, &lock);
		request_t *request = pending;
		pending = NULL;
		pthread_mutex_unlock(&lock);
		result_t *result = calloc(1, sizeof(*result));
		if (result) {
			result->generation = request->generation;
			result->scan = request->scan;
			if (request->scan) ebookdict_scan(request->root, &result->catalog, cancelled, request);
			else ebookdict_lookup(request->root, request->id, request->query, &result->definition, cancelled, request);
			pthread_mutex_lock(&lock);
			if (!cancelled(request)) { free(completed); completed = result; result = NULL; }
			pthread_mutex_unlock(&lock);
			free(result);
		} else if (!cancelled(request)) atomic_store(&failed_generation, request->generation);
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
}

static bool start_worker(void) {
	if (started) return true;
	pthread_attr_t attr;
	if (pthread_attr_init(&attr)) return false;
	if (pthread_attr_setstacksize(&attr, 128u * 1024u)) { pthread_attr_destroy(&attr); return false; }
	pthread_t thread;
	int error = pthread_create(&thread, &attr, worker, NULL);
	pthread_attr_destroy(&attr);
	if (error) return false;
	pthread_detach(thread);
	started = true;
	return true;
}

static void keyboard_show(bool show) {
	keyboard_set_visible(keyboard, show);
	int height = (int)cfg->screen_height - body_top - (show ? KEYBOARD_H : 0);
	lv_obj_set_height(body, height > 32 ? height : 32);
}

static void reset_definition(void) {
	lv_label_set_text(meaning, "");
	lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
}

static bool queue(bool scan) {
	if (!cfg->sd_root_path || strlen(cfg->sd_root_path) >= sizeof(((request_t *)0)->root)) {
		lv_label_set_text(notice, "The SD card is unavailable."); return false;
	}
	if (!start_worker()) { lv_label_set_text(notice, "Could not start dictionary lookup. Try reopening it."); return false; }
	request_t *request = calloc(1, sizeof(*request));
	if (!request) { lv_label_set_text(notice, "Not enough memory for dictionary lookup."); return false; }
	cancel_work();
	request->generation = atomic_load(&generation);
	request->scan = scan;
	memcpy(request->root, cfg->sd_root_path, strlen(cfg->sd_root_path) + 1);
	memcpy(request->id, selected_id, strlen(selected_id) + 1);
	const char *text = lv_textarea_get_text(field);
	if (strlen(text) >= sizeof(request->query)) {
		free(request); lv_label_set_text(notice, "The word or phrase is too long."); return false;
	}
	memcpy(request->query, text, strlen(text) + 1);
	pthread_mutex_lock(&lock);
	pending = request;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
	lv_timer_resume(timer);
	return true;
}

static void render_catalog(const ebookdict_catalog_t *found) {
	catalog = *found;
	populating = true;
	if (!catalog.count) {
		lv_dropdown_set_options(picker, "No dictionaries");
		selected_id[0] = 0;
		lv_label_set_text(notice, "Add the dictionary pack to Ebook/Dictionaries on the SD card, then reopen Dictionary.");
	} else {
		char choices[EBOOKDICT_MAX * EBOOKDICT_ID];
		size_t used = 0;
		unsigned selected = 0;
		for (unsigned i = 0; i < catalog.count; i++) {
			if (!strcmp(selected_id, catalog.entries[i].id)) selected = i;
			if (i) choices[used++] = '\n';
			size_t n = strlen(catalog.entries[i].name);
			memcpy(choices + used, catalog.entries[i].name, n); used += n;
		}
		choices[used] = 0;
		lv_dropdown_set_options(picker, choices);
		lv_dropdown_set_selected(picker, selected);
		memcpy(selected_id, catalog.entries[selected].id, strlen(catalog.entries[selected].id) + 1);
		lv_label_set_text(notice, catalog.truncated ? "Dictionary list limit reached. Choose a dictionary and press Lookup." :
			"Enter a word or phrase, then press Lookup.");
	}
	populating = false;
}

static void render_definition(const ebookdict_result_t *result) {
	const char *message;
	switch (result->status) {
	case EBOOKDICT_OK:
		lv_label_set_text(meaning, result->text);
		lv_label_set_text_fmt(notice, result->truncated ? "%s - long entry; showing the beginning." : "%s - offline definition", result->word);
		return;
	case EBOOKDICT_NOT_FOUND: message = "No entry found. Try the base form or another dictionary."; break;
	case EBOOKDICT_EMPTY: message = "Enter a word or phrase."; break;
	case EBOOKDICT_LONG_QUERY: message = "The word or phrase is too long."; break;
	case EBOOKDICT_BAD_QUERY: message = "This lookup text could not be read. Please retype it."; break;
	case EBOOKDICT_BAD_FILE: message = "This dictionary is damaged or unsupported. Replace the pack and try again."; break;
	case EBOOKDICT_CANCELLED: return;
	default: message = "The dictionary is unavailable. Check the SD card and reopen Dictionary."; break;
	}
	lv_label_set_text(notice, message);
}

static void poll_cb(lv_timer_t *t) {
	if (!active) { lv_timer_pause(t); return; }
	pthread_mutex_lock(&lock);
	result_t *result = completed;
	completed = NULL;
	pthread_mutex_unlock(&lock);
	if (!result) {
		if (atomic_load(&failed_generation) == atomic_load(&generation)) {
			loading = false;
			lv_label_set_text(notice, "Not enough memory for dictionary lookup. Try reopening it.");
			lv_timer_pause(t);
		}
		return;
	}
	if (result->generation == atomic_load(&generation)) {
		if (result->scan) { loading = false; render_catalog(&result->catalog); }
		else render_definition(&result->definition);
		lv_timer_pause(t);
	}
	free(result);
}

static void lookup_cb(lv_event_t *e) {
	(void)e;
	if (!active) return;
	if (loading) { lv_label_set_text(notice, "The dictionary list is still loading."); return; }
	if (!catalog.count || !selected_id[0]) {
		lv_label_set_text(notice, "Install the dictionary pack on the SD card first."); return;
	}
	reset_definition();
	if (queue(false)) { keyboard_show(false); lv_label_set_text(notice, "Looking up offline..."); }
}

static void field_clicked_cb(lv_event_t *e) { (void)e; if (active) keyboard_show(true); }
static void changed_cb(lv_event_t *e) {
	(void)e;
	if (!active) return;
	reset_definition();
	if (!loading) { cancel_work(); lv_timer_pause(timer); lv_label_set_text(notice, "Press Lookup to search offline."); }
}
static void picker_cb(lv_event_t *e) {
	(void)e;
	if (!active || populating || loading) return;
	unsigned selected = lv_dropdown_get_selected(picker);
	if (selected >= catalog.count) return;
	memcpy(selected_id, catalog.entries[selected].id, strlen(catalog.entries[selected].id) + 1);
	changed_cb(NULL);
}
static void clear_cb(lv_event_t *e) { (void)e; lv_textarea_set_text(field, ""); keyboard_show(true); }

void ebookdictionary_abort(void) {
	active = loading = false;
	cancel_work();
	if (overlay) {
		keyboard_show(false);
		reset_definition();
		lv_obj_set_hidden(overlay, true);
		lv_dropdown_close(picker);
	}
	if (timer) lv_timer_pause(timer);
}
static void back_cb(lv_event_t *e) {
	(void)e;
	ebookdictionary_abort();
	if (readingkeys_active()) readingkeys_enter(readingkeys_mode());
}

static void button(const char *text, int x, int y, int width, lv_event_cb_t cb) {
	lv_obj_t *obj = lv_btn_create(overlay);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, width, 52);
	lv_obj_add_style(obj, &theme_style_card, 0);
	lv_obj_add_style(obj, &theme_style_card_pressed, LV_STATE_PRESSED);
	lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *label = lv_label_create(obj);
	lv_label_set_text(label, text);
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_obj_set_style_text_font(label, &font_ui_20, 0);
	lv_obj_center(label);
}

static void build(void) {
	int pad = cfg->padding, width = (int)cfg->screen_width;
	overlay = lv_obj_create(reader);
	lv_obj_remove_style_all(overlay);
	lv_obj_add_style(overlay, &theme_style_screen, 0);
	lv_obj_set_size(overlay, width, cfg->screen_height);
	lv_obj_set_scrollable(overlay, false);
	button("Back", pad, pad, 84, back_cb);
	lv_obj_t *title = lv_label_create(overlay);
	lv_label_set_text(title, "Dictionary");
	lv_obj_set_pos(title, pad + 100, pad + 12);
	lv_obj_add_style(title, &theme_style_text, 0);
	lv_obj_set_style_text_font(title, &font_ui_24, 0);
	picker = lv_dropdown_create(overlay);
	lv_obj_set_pos(picker, pad, pad + 64);
	lv_obj_set_size(picker, width - 2 * pad, 52);
	lv_obj_add_style(picker, &theme_style_card, 0);
	lv_obj_set_style_text_font(picker, &font_ui_20, 0);
	lv_obj_add_event_cb(picker, picker_cb, LV_EVENT_VALUE_CHANGED, NULL);
	field = lv_textarea_create(overlay);
	lv_textarea_set_one_line(field, true);
	lv_textarea_set_max_length(field, 60);
	lv_textarea_set_placeholder_text(field, "Word or phrase");
	lv_obj_set_pos(field, pad, pad + 124);
	lv_obj_set_size(field, width - 2 * pad - 84, 56);
	lv_obj_add_style(field, &theme_style_card, 0);
	lv_obj_set_style_text_font(field, &font_ui_20, 0);
	keyboard_style_caret(field);
	lv_obj_add_event_cb(field, field_clicked_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(field, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
	button("Clear", width - pad - 76, pad + 126, 76, clear_cb);
	notice = lv_label_create(overlay);
	lv_obj_set_pos(notice, pad, pad + 188);
	lv_obj_set_size(notice, width - 2 * pad, 54);
	lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
	lv_obj_add_style(notice, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(notice, &font_ui_18, 0);
	body_top = pad + 250;
	body = lv_obj_create(overlay);
	lv_obj_remove_style_all(body);
	lv_obj_set_pos(body, pad, body_top);
	lv_obj_set_width(body, width - 2 * pad);
	meaning = lv_label_create(body);
	lv_obj_set_width(meaning, lv_pct(100));
	lv_label_set_long_mode(meaning, LV_LABEL_LONG_WRAP);
	lv_obj_add_style(meaning, &theme_style_text, 0);
	lv_obj_set_style_text_font(meaning, &font_ui_20, 0);
	keyboard = keyboard_create(overlay, width, KEYBOARD_H, field, NULL, "Lookup", lookup_cb, NULL);
}

void ebookdictionary_init(gui_config_t *config, lv_obj_t *parent) {
	cfg = config; reader = parent;
	timer = lv_timer_create(poll_cb, 100, NULL);
	lv_timer_pause(timer);
}
void ebookdictionary_open(void) {
	if (!overlay) build();
	ebookdictionary_abort();
	memset(&catalog, 0, sizeof(catalog));
	lv_dropdown_set_options(picker, "Loading dictionaries...");
	lv_textarea_set_text(field, "");
	active = loading = true;
	keyboard_reset(keyboard);
	keyboard_show(true);
	lv_obj_set_hidden(overlay, false);
	lv_obj_move_foreground(overlay);
	lv_label_set_text(notice, "Reading dictionary list from the SD card...");
	if (!queue(true)) loading = false;
	if (readingkeys_active()) readingkeys_enter(readingkeys_mode());
}
bool ebookdictionary_key(gui_key_t key) {
	if (!active) return false;
	if (key == GUI_KEY_PLAY_PAUSE) back_cb(NULL);
	return true;
}
