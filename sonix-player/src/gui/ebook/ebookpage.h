#ifndef EBOOKPAGE_H
#define EBOOKPAGE_H

#include "src/gui/shell/gui.h"

#include "lvgl/lvgl.h"

extern lv_obj_t *ebookpage_screen;

void ebookpage_init(gui_config_t *cfg);

// Reads the current Ebook folder in the background and shows the shelf.
// Search spans all Ebook subfolders. What the tile on the Home
// page calls, so a book copied onto the card while the player was running
// turns up without a restart.
void ebookpage_open(void);
// Opens the most recently read book available on the current card, or its shelf.
void ebookpage_continue(void);

#endif /* EBOOKPAGE_H */
