#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/core/config.h"
#include "src/system/input/readingkeys.h"

static char config_file[512];

static void set_mode(readingkeys_mode_t mode) {
	readingkeys_set_mode(mode);
#ifdef _WIN32
	/* The deployed Linux config writer atomically replaces an existing file.
	 * Native Windows CRT rename needs a test-only remove before a dirty retry. */
	if (!config_store_save(config_ebook_store())) {
		remove(config_file);
		assert(config_store_save(config_ebook_store()));
	}
#endif
	assert(config_store_init(config_ebook_store(), config_file));
}

static readingkeys_event_t click(readingkeys_tracker_t *tracker, keymap_button_t button) {
	readingkeys_event_t event = UINT32_MAX;
	assert(readingkeys_handle(tracker, button, 1, false, false, &event) && !event);
	/* Arbitrarily long/kernel-repeated holds must not produce page bursts. */
	for (int i = 0; i < 40; i++) {
		event = UINT32_MAX;
		assert(readingkeys_handle(tracker, button, 2, false, false, &event) && !event);
	}
	assert(readingkeys_handle(tracker, button, 0, false, false, &event) && event);
	readingkeys_event_t duplicate = UINT32_MAX;
	assert(!readingkeys_handle(tracker, button, 0, false, false, &duplicate) && !duplicate);
	return event;
}

int main(int argc, char **argv) {
	assert(argc == 2);
	assert(snprintf(config_file, sizeof(config_file), "%s/ebook.ini", argv[1]) < (int)sizeof(config_file));
	config_store_t *store = config_ebook_store();
	(void)config_store_init(store, config_file);
	config_store_set_int(store, "ebook", "font_size", 26);
	config_store_set(store, "ebook", "pos_existing", "3:14:8|p1:37:0");
	assert(readingkeys_mode() == READINGKEYS_VOLUME_NORMAL);
	assert(!readingkeys_active());
	readingkeys_tracker_t keys = {0};
	readingkeys_event_t event;
	for (int button = 0; button < KEYMAP_BTN_COUNT; button++) {
		assert(!readingkeys_handle(&keys, button, 1, false, false, &event));
		assert(!readingkeys_handle(&keys, button, 0, false, false, &event));
	}
	readingkeys_enter(readingkeys_mode());
	assert(readingkeys_active());
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_NEXT)) == READINGKEYS_NEXT);
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_PREV)) == READINGKEYS_PREV);
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_PLAY)) == READINGKEYS_MENU);
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_VOL_DOWN, 1, false, false, &event));
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_COUNT, 1, false, false, &event)); /* power/unknown */
	assert(!readingkeys_handle(NULL, KEYMAP_BTN_NEXT, 1, false, false, &event));
	assert(!readingkeys_handle(&keys, (keymap_button_t)-1, 1, false, false, &event));
	assert(readingkeys_action(0) == READINGKEYS_NONE);
	assert(readingkeys_action(UINT32_MAX) == READINGKEYS_NONE);

	/* Settings persist and a mode change invalidates queued actions. */
	readingkeys_event_t queued = click(&keys, KEYMAP_BTN_NEXT);
	set_mode(READINGKEYS_VOLUME_UP_PREV);
	assert(readingkeys_mode() == READINGKEYS_VOLUME_UP_PREV);
	assert(readingkeys_action(queued) == READINGKEYS_NONE);
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_VOL_UP)) == READINGKEYS_PREV);
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_VOL_DOWN)) == READINGKEYS_NEXT);
	set_mode(READINGKEYS_VOLUME_UP_NEXT);
	assert(readingkeys_mode() == READINGKEYS_VOLUME_UP_NEXT);
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_VOL_UP)) == READINGKEYS_NEXT);
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_VOL_DOWN)) == READINGKEYS_PREV);
	/* Neither a cable remote nor a Bluetooth/headset-origin volume command is
	 * claimed as a physical case button by this routing layer. */
	for (int button = 0; button < KEYMAP_BTN_COUNT; button++) {
		assert(!readingkeys_handle(&keys, button, 1, true, false, &event));
		assert(!readingkeys_handle(&keys, button, 0, true, false, &event));
	}

	/* Power combinations consume volume gestures without turning a page. */
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 0, false, true, &event) && !event);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_DOWN, 1, false, true, &event));
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_DOWN, 0, false, false, &event) && !event);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 2, false, true, &event) && !event);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 0, false, false, &event) && !event);

	/* Simultaneous case keys each retain their own release, including when
	 * the kernel reports the keys on different input nodes/trackers. */
	readingkeys_tracker_t another_node = {0};
	assert(readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 1, false, false, &event));
	assert(readingkeys_handle(&keys, KEYMAP_BTN_PLAY, 1, false, false, &event));
	assert(readingkeys_handle(&another_node, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	assert(readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 0, false, false, &event));
	assert(readingkeys_action(event) == READINGKEYS_NEXT);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_PLAY, 0, false, false, &event));
	assert(readingkeys_action(event) == READINGKEYS_MENU);
	assert(readingkeys_handle(&another_node, KEYMAP_BTN_VOL_UP, 0, false, false, &event));
	assert(readingkeys_action(event) == READINGKEYS_NEXT);

	/* A reader-owned release must never become music input after closing the
	 * reader. Already queued reading commands also expire on screen changes. */
	queued = click(&keys, KEYMAP_BTN_NEXT);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 1, false, false, &event));
	uint32_t epoch = readingkeys_epoch();
	readingkeys_leave();
	assert(!readingkeys_active() && readingkeys_epoch() != epoch);
	assert(readingkeys_action(queued) == READINGKEYS_NONE);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 0, false, false, &event));
	assert(readingkeys_action(event) == READINGKEYS_NONE); /* consumed, not audio fallback */
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 1, false, false, &event));
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 0, false, false, &event));
	readingkeys_enter(READINGKEYS_VOLUME_UP_PREV);
	assert(readingkeys_action(queued) == READINGKEYS_NONE); /* a different book/session */
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	set_mode(READINGKEYS_VOLUME_UP_NEXT);
	assert(readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 0, false, false, &event));
	assert(readingkeys_action(event) == READINGKEYS_NONE); /* mode changed while held */

	/* Recover a missing release when the same key is pressed by a new owner. */
	assert(readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 1, false, false, NULL));
	readingkeys_leave();
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 1, false, false, &event));
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_NEXT, 0, false, false, &event));
	set_mode(READINGKEYS_VOLUME_NORMAL);
	assert(!readingkeys_active()); /* changing preferences outside a book doesn't enter it */
	readingkeys_enter(READINGKEYS_VOLUME_NORMAL);
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	assert(readingkeys_action(click(&keys, KEYMAP_BTN_NEXT)) == READINGKEYS_NEXT);
	config_store_set_int(store, "ebook", "volume_pages", -100);
	assert(readingkeys_mode() == READINGKEYS_VOLUME_NORMAL);
	config_store_set_int(store, "ebook", "volume_pages", 999);
	assert(readingkeys_mode() == READINGKEYS_VOLUME_NORMAL);
	readingkeys_enter((readingkeys_mode_t)999);
	assert(!readingkeys_handle(&keys, KEYMAP_BTN_VOL_UP, 1, false, false, &event));
	assert(config_store_get_int(store, "ebook", "font_size", 0) == 26);
	assert(!strcmp(config_store_get(store, "ebook", "pos_existing", ""), "3:14:8|p1:37:0"));
	readingkeys_leave();
	puts("reading keys: mappings, one action per hold, settings, combinations, stale releases and session changes passed");
	return 0;
}
