#ifndef EBOOKDICTIONARY_H
#define EBOOKDICTIONARY_H

#include "src/gui/shell/gui.h"

void ebookdictionary_init(gui_config_t *cfg, lv_obj_t *reader);
void ebookdictionary_open(void);
bool ebookdictionary_key(gui_key_t key);
void ebookdictionary_abort(void);

#endif
