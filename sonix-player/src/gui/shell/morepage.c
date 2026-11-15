#include "morepage.h"

#include "lvgl/lvgl.h"

#include "src/gui/audio/dacpage.h"
#include "src/gui/audio/metronomepage.h"
#include "src/gui/library/audiobooks.h"
#include "src/gui/library/filespage.h"
#include "src/gui/streaming/streaming.h"
#include "src/gui/wireless/wireless.h"
#include "src/gui/shell/gridpage.h"
#include "src/gui/settings/fwupdate.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/settingsrow.h"

lv_obj_t *morepage_screen;

void morepage_init(gui_config_t *cfg) {
	const grid_entry_t entries[] = {
		{"audiobooks", &icon_menu_audiobooks, &audiobooks_screen, NULL},
		{"streaming", &icon_menu_streaming, &streaming_screen, NULL},
		{"wireless", &icon_menu_wireless, &wireless_screen, NULL},
		{"file_explorer", &icon_menu_file_explorer, NULL, filespage_open},
		{"dac", &icon_menu_dac, &dacpage_screen, NULL},
		{"metronome", &icon_menu_metronome, &metronomepage_screen, NULL},
		{"Game Store", &icon_menu_wifi_transfer, NULL, fwupdate_show_game_store},
	};

	// Two columns by three rows, like the Music and Wireless pages, so a tile
	// here is exactly the size of a tile there. The grid is laid out as full
	// even with fewer entries, so the only tile sits where it would with six --
	// top left -- instead of floating in the middle of a page of its own.
	lv_obj_t *grid = gridpage_build(morepage_screen, cfg, entries, (int)(sizeof(entries) / sizeof(entries[0])), 2, 3, true);
	gridpage_set_scrolling(grid);

	settingsrow_title(morepage_screen, cfg, "more");
}
