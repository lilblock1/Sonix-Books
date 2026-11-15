#ifndef EBOOKFIND_H
#define EBOOKFIND_H

#include "src/gui/shell/gui.h"
#include "src/system/ebook/ebook.h"

void ebookfind_init(gui_config_t *cfg, lv_obj_t *reader,
					void (*restore)(ebook_position_t position, bool selected));
/* Reader releases its displayed chapter before opening this overlay. */
void ebookfind_open(const char *path, ebook_position_t origin);
bool ebookfind_active(void);
bool ebookfind_key(gui_key_t key);
/* Reader unload: cancel without restoring its chapter or keeping UI results. */
void ebookfind_abort(void);

#endif
