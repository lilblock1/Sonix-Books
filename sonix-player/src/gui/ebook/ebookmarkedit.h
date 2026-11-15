#ifndef EBOOKMARKEDIT_H
#define EBOOKMARKEDIT_H

#include "src/gui/shell/gui.h"
#include "src/system/ebook/bookmarks.h"

// One lazy, reusable keyboard overlay, shared by the reader and bookmark list.
// The caller owns no field pointers. Cancel/unload never writes an edit.
bool ebookmarkedit_open(gui_config_t *cfg, lv_obj_t *owner, const char *path,
	int index, const bookmark_t *mark, void (*saved)(void));
bool ebookmarkedit_active(lv_obj_t *owner);
void ebookmarkedit_abort(lv_obj_t *owner);

#endif
