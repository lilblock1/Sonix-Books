#include "readingkeys.h"

#include <stdatomic.h>
#include "src/system/core/config.h"

#define FLAG_ACTIVE 1u
#define FLAGS_MASK 255u
#define EPOCH_STEP 256u
#define EVENT_CANCELLED 128u

static _Atomic uint32_t context;

static readingkeys_mode_t valid_mode(long mode) {
	return mode >= 0 && mode < READINGKEYS_VOLUME_MODE_COUNT ? (readingkeys_mode_t)mode : READINGKEYS_VOLUME_NORMAL;
}

readingkeys_mode_t readingkeys_mode(void) {
	return valid_mode(config_store_get_int(config_ebook_store(), "ebook", "volume_pages", READINGKEYS_VOLUME_NORMAL));
}

static void publish(uint32_t flags) {
	/* One UI-thread writer. Skip zero so a valid event is never the empty slot. */
	uint32_t epoch = (atomic_load(&context) & ~FLAGS_MASK) + EPOCH_STEP;
	if (!epoch) epoch = EPOCH_STEP;
	atomic_store(&context, epoch | flags);
}

void readingkeys_enter(readingkeys_mode_t mode) { publish(FLAG_ACTIVE | ((uint32_t)valid_mode(mode) << 1)); }
void readingkeys_leave(void) { publish(0); }
bool readingkeys_active(void) { return (atomic_load(&context) & FLAG_ACTIVE) != 0; }
uint32_t readingkeys_epoch(void) { return atomic_load(&context) & ~FLAGS_MASK; }

void readingkeys_set_mode(readingkeys_mode_t mode) {
	mode = valid_mode(mode);
	config_store_set_int(config_ebook_store(), "ebook", "volume_pages", mode);
	config_store_save(config_ebook_store());
	if (readingkeys_active()) readingkeys_enter(mode);
}

static readingkeys_event_t capture(keymap_button_t button) {
	uint32_t state = atomic_load(&context);
	if (!(state & FLAG_ACTIVE)) return 0;
	readingkeys_action_t action = READINGKEYS_NONE;
	switch (button) {
	case KEYMAP_BTN_PLAY: action = READINGKEYS_MENU; break;
	case KEYMAP_BTN_PREV: action = READINGKEYS_PREV; break;
	case KEYMAP_BTN_NEXT: action = READINGKEYS_NEXT; break;
	case KEYMAP_BTN_VOL_UP:
	case KEYMAP_BTN_VOL_DOWN: {
		readingkeys_mode_t mode = valid_mode((state & FLAGS_MASK) >> 1);
		if (mode == READINGKEYS_VOLUME_NORMAL) break;
		bool next = (button == KEYMAP_BTN_VOL_DOWN) == (mode == READINGKEYS_VOLUME_UP_PREV);
		action = next ? READINGKEYS_NEXT : READINGKEYS_PREV;
		break;
	}
	default: break;
	}
	return action == READINGKEYS_NONE ? 0 : (state & ~FLAGS_MASK) | (uint32_t)action;
}

bool readingkeys_handle(readingkeys_tracker_t *tracker, keymap_button_t button, int value,
						bool headset, bool combination, readingkeys_event_t *event) {
	if (event) *event = 0;
	if (!tracker || headset || button < 0 || button >= KEYMAP_BTN_COUNT) return false;
	if (value == 1) {
		/* A fresh press replaces a latch whose release was lost. */
		tracker->held[button] = capture(button);
		if (combination && tracker->held[button]) tracker->held[button] |= EVENT_CANCELLED;
		return tracker->held[button] != 0;
	}
	readingkeys_event_t held = tracker->held[button];
	if (!held) return false;
	if (value == 0) {
		tracker->held[button] = 0;
		if (!combination && !(held & EVENT_CANCELLED) && event) *event = held;
	} else if (combination) {
		tracker->held[button] |= EVENT_CANCELLED;
	}
	return true;
}

readingkeys_action_t readingkeys_action(readingkeys_event_t event) {
	uint32_t state = atomic_load(&context);
	uint32_t action = event & FLAGS_MASK;
	if (!(state & FLAG_ACTIVE) || (state & ~FLAGS_MASK) != (event & ~FLAGS_MASK) ||
		action < READINGKEYS_MENU || action > READINGKEYS_NEXT) return READINGKEYS_NONE;
	return (readingkeys_action_t)action;
}
