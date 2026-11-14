#include "gamespage.h"

#include "src/gui/flappybird/flappybird.h"
#include "src/gui/gearboy/gearboypage.h"
#include "src/gui/shell/gridpage.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/settingsrow.h"

lv_obj_t *gamespage_screen;

void gamespage_init(gui_config_t *cfg) {
	const grid_entry_t entries[] = {
#if GB_CORE
		{"gearboy", &icon_menu_gearboy, &gearboypage_screen, NULL},
#endif
		{"flappy_bird", &icon_menu_flappy_bird, NULL, flappybird_open},
	};
	gridpage_build(gamespage_screen, cfg, entries, (int)(sizeof(entries) / sizeof(entries[0])), 2, 2, true);
	settingsrow_title(gamespage_screen, cfg, "books_games");
}
