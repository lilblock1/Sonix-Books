#ifndef GAMELOGO_H
#define GAMELOGO_H
#include "lvgl/lvgl.h"
#include "src/system/device/game_store.h"
// Copies the bounded artwork; can outlive a catalog refresh. For older
// packages, the installed card is checked before using a local fallback.
lv_obj_t *gamelogo_create(lv_obj_t *parent,const char *id,const char *title,game_store_kind_t kind,const uint8_t *art);
#endif
