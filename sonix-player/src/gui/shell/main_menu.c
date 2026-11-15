#include "main_menu.h"

#include <stdint.h>

#include "src/gui/ebook/ebookpage.h"
#include "src/gui/shell/gamespage.h"
#include "src/gui/shell/gridpage.h"
#include "src/gui/shell/gui.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/morepage.h"
#include "src/gui/library/music.h"
#include "src/gui/settings/settings.h"
#include "src/gui/shell/switcher.h"

#include "lvgl/lvgl.h"

lv_obj_t *main_menu_screen;

void main_menu_init(gui_config_t *cfg) {
	// Reading and games are the primary destinations in this fork. Actions
	// resolve the current card on entry rather than retaining a stale path.
	const grid_entry_t entries[] = {
		{"books", &icon_menu_books, NULL, ebookpage_open},
		{"books_continue", &icon_menu_books, NULL, ebookpage_continue},
		{"Game Launcher", &icon_menu_gearboy, &gamespage_screen, NULL},
		{"music", &icon_menu_music, &music_screen, NULL},
		{"more", &icon_menu_more, &morepage_screen, NULL},
		{"settings", &icon_menu_settings, &settings_screen, NULL},
	};

	gridpage_build(main_menu_screen, cfg, entries, (int)(sizeof(entries) / sizeof(entries[0])), 2, 3, false);
}
