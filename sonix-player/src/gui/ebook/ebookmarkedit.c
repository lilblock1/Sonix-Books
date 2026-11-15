#include "ebookmarkedit.h"

#include <stdio.h>
#include <string.h>

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/lang.h"
#include "src/system/input/readingkeys.h"

#define KEYBOARD_H 316

static lv_obj_t *overlay, *owner, *name_field, *note_field, *notice;
static keyboard_t *keyboard;
static char book_path[800];
static bookmark_t original;
static int mark_index;
static bool active;
static void (*saved_cb)(void);

static void hide(void) {
	active = false;
	keyboard_set_visible(keyboard, false);
	lv_obj_set_hidden(overlay, true);
	owner = NULL;
	saved_cb = NULL;
	// Discard page presses queued while the keyboard owned the reader.
	if (readingkeys_active()) readingkeys_enter(readingkeys_mode());
}

static void cancel_cb(lv_event_t *e) { (void)e; hide(); }

static void save_cb(lv_event_t *e) {
	(void)e;
	if (!active) return;
	const char *name = lv_textarea_get_text(name_field);
	const char *note = lv_textarea_get_text(note_field);
	if (strlen(name) >= sizeof(original.name) || strlen(note) >= sizeof(original.note)) {
		lv_label_set_text(notice, "Shorten the name or note to save it.");
		return;
	}
	bookmark_t edited = original;
	memcpy(edited.name, name, strlen(name) + 1);
	memcpy(edited.note, note, strlen(note) + 1);
	bookmark_result_t result = bookmark_save(book_path, mark_index, &edited);
	if (result != BOOKMARK_OK) {
		const char *message = result == BOOKMARK_FULL ? "This book has 64 bookmarks. Delete one first." :
			result == BOOKMARK_STORAGE_FULL ? "Bookmark storage is full. Delete an unused bookmark and try again." :
			result == BOOKMARK_INVALID ? "This bookmark cannot be saved. Reopen it and try again." :
			"Could not save. Your changes are still here; try again or cancel.";
		lv_label_set_text(notice, message);
		return;
	}
	void (*done)(void) = saved_cb;
	hide();
	if (done) done();
}

static void field_cb(lv_event_t *e) {
	lv_obj_t *field = lv_event_get_current_target(e);
	keyboard_set_field(keyboard, field);
	keyboard_show_caret(name_field, field == name_field);
	keyboard_show_caret(note_field, field == note_field);
}

static void tab_cb(lv_event_t *e) {
	bool notes = lv_event_get_user_data(e) != NULL;
	lv_obj_set_hidden(name_field, notes);
	lv_obj_set_hidden(note_field, !notes);
	keyboard_set_field(keyboard, notes ? note_field : name_field);
	keyboard_show_caret(name_field, !notes);
	keyboard_show_caret(note_field, notes);
}

static lv_obj_t *button(const char *text, int x, int y, int width, lv_event_cb_t cb, void *user) {
	lv_obj_t *obj = lv_btn_create(overlay);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, width, 52);
	lv_obj_add_style(obj, &theme_style_card, 0);
	lv_obj_add_style(obj, &theme_style_card_pressed, LV_STATE_PRESSED);
	lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
	lv_obj_t *label = lv_label_create(obj);
	lv_label_set_text(label, tr(text));
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_obj_set_style_text_font(label, &font_ui_20, 0);
	lv_obj_center(label);
	return obj;
}

static lv_obj_t *field(gui_config_t *cfg, int top, bool notes) {
	lv_obj_t *obj = lv_textarea_create(overlay);
	lv_textarea_set_one_line(obj, !notes);
	lv_textarea_set_max_length(obj, notes ? BOOKMARK_NOTE_MAX - 1 : BOOKMARK_NAME_MAX - 1);
	lv_textarea_set_placeholder_text(obj, notes ? "Note (optional)" : "Bookmark name (optional)");
	lv_obj_set_pos(obj, cfg->padding, top);
	int height = (int)cfg->screen_height - KEYBOARD_H - top - 64;
	lv_obj_set_size(obj, (int)cfg->screen_width - 2 * cfg->padding, height > 64 ? height : 64);
	lv_obj_add_style(obj, &theme_style_card, 0);
	lv_obj_set_style_text_font(obj, &font_ui_20, 0);
	keyboard_style_caret(obj);
	lv_obj_add_event_cb(obj, field_cb, LV_EVENT_CLICKED, NULL);
	return obj;
}

static void build(gui_config_t *cfg) {
	int pad = cfg->padding, width = (int)cfg->screen_width;
	overlay = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(overlay);
	lv_obj_add_style(overlay, &theme_style_screen, 0);
	lv_obj_set_size(overlay, width, cfg->screen_height);
	lv_obj_set_scrollable(overlay, false);
	button("cancel", pad, pad, 84, cancel_cb, NULL);
	button("save", width - pad - 84, pad, 84, save_cb, NULL);
	lv_obj_t *title = lv_label_create(overlay);
	lv_label_set_text(title, "Bookmark");
	lv_obj_set_width(title, width - 2 * pad - 184);
	lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
	lv_obj_set_pos(title, pad + 92, pad + 14);
	lv_obj_add_style(title, &theme_style_text, 0);
	lv_obj_set_style_text_font(title, &font_ui_20, 0);
	button("Name", pad, pad + 64, (width - 2 * pad - 8) / 2, tab_cb, NULL);
	button("Note", width / 2 + 4, pad + 64, (width - 2 * pad - 8) / 2, tab_cb, (void *)1);
	name_field = field(cfg, pad + 128, false);
	note_field = field(cfg, pad + 128, true);
	notice = lv_label_create(overlay);
	lv_obj_set_pos(notice, pad, (int)cfg->screen_height - KEYBOARD_H - 58);
	lv_obj_set_size(notice, width - 2 * pad, 54);
	lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
	lv_obj_add_style(notice, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(notice, &font_ui_18, 0);
	keyboard = keyboard_create(overlay, width, KEYBOARD_H, name_field, NULL, "save", save_cb, NULL);
	lv_obj_set_hidden(overlay, true);
}

bool ebookmarkedit_open(gui_config_t *cfg, lv_obj_t *source, const char *path,
	int index, const bookmark_t *mark, void (*saved)(void)) {
	if (!cfg || !source || !path || !path[0] || strlen(path) >= sizeof(book_path) || !mark) return false;
	if (!overlay) build(cfg);
	original = *mark;
	mark_index = index;
	memcpy(book_path, path, strlen(path) + 1);
	owner = source;
	saved_cb = saved;
	active = true;
	lv_textarea_set_text(name_field, mark->name);
	lv_textarea_set_text(note_field, mark->note);
	lv_obj_set_hidden(name_field, false);
	lv_obj_set_hidden(note_field, true);
	lv_label_set_text(notice, "Blank fields keep a plain bookmark. Cancel discards edits.");
	keyboard_reset(keyboard);
	keyboard_set_field(keyboard, name_field);
	keyboard_show_caret(name_field, true);
	keyboard_show_caret(note_field, false);
	keyboard_set_visible(keyboard, true);
	lv_obj_set_hidden(overlay, false);
	lv_obj_move_foreground(overlay);
	if (readingkeys_active()) readingkeys_enter(readingkeys_mode());
	return true;
}

bool ebookmarkedit_active(lv_obj_t *source) { return active && owner == source; }
void ebookmarkedit_abort(lv_obj_t *source) { if (ebookmarkedit_active(source)) hide(); }
