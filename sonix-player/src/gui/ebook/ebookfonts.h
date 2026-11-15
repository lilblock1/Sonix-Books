#ifndef EBOOKFONTS_H
#define EBOOKFONTS_H

#include <stdbool.h>

#include "lvgl/lvgl.h"

// ---------------------------------------------------------------------------
// Reading faces, loaded only while reading; optional SD/Ebook/Fonts selection.
//
// Bookerly, in the four real styles rather than one face slanted and
// emboldened by the renderer. The firmware ships them in
// usr/resource/sonix/fonts/ebook; a set in `.local/fonts/` on the card is used
// when that folder has none.
//
// So: nothing is loaded until the reader opens, the regular face is loaded
// then, and the other three only when a book actually uses them -- a novel
// with no bold never pays for the bold face. Closing the reader destroys all
// four and the glyph cache with them.
//
// The one rule that cannot be broken: no LVGL object may still point at one of
// these fonts when it is destroyed. ebookfonts_close() is therefore called
// after the reader's objects are deleted, never before.
// ---------------------------------------------------------------------------

// Opens the regular face at `size` pixels. False when neither folder has
// Bookerly -- the reader then falls back to the interface font, which is
// a worse book but still a book.
//
// `japanese` sets the book in the interface's Japanese faces (Rodin, regular
// and bold, from the firmware's fonts folder or the card's .local/fonts)
// instead: Bookerly has no kana or kanji.
bool ebookfonts_open(const char *sd_root, int size, bool japanese);

// Independent face for a preview label. Detach the label, then delete the face
// with lv_freetype_font_delete(). Empty id previews the automatic Default.
lv_font_t *ebookfonts_preview(const char *sd_root, const char *id, int size, bool japanese);

// Apply/save a validated relative SD font ID (empty restores Default). All
// labels using active faces must be removed before this call. A bad custom
// candidate leaves the old active faces and settings intact.
bool ebookfonts_choose(const char *sd_root, const char *id, int size, bool japanese);
const char *ebookfonts_active_id(void);

// Everything above, undone. Safe when nothing was opened.
void ebookfonts_close(void);

// Re-opens every face that is currently loaded at a new size. What the font
// size setting calls.
bool ebookfonts_set_size(int size);
int ebookfonts_size(void);

// The face for a run of text. `bold` and `italic` come from the book; a face
// that is not on the card falls back the way the markdown asks -- bold italic
// to italic to bold to regular -- rather than making the book unreadable.
const lv_font_t *ebookfonts_face(bool bold, bool italic);

// True when the selected regular face (or automatic Default) was found. The settings page says so, because a reader
// that quietly uses the interface font looks like a reader with a bug.
bool ebookfonts_present(void);

#endif /* EBOOKFONTS_H */
