#ifndef READINGKEYS_H
#define READINGKEYS_H

#include <stdint.h>
#include "keymap.h"

typedef enum {
	READINGKEYS_VOLUME_NORMAL = 0,
	READINGKEYS_VOLUME_UP_PREV,
	READINGKEYS_VOLUME_UP_NEXT,
	READINGKEYS_VOLUME_MODE_COUNT
} readingkeys_mode_t;

typedef enum {
	READINGKEYS_NONE = 0,
	READINGKEYS_MENU,
	READINGKEYS_PREV,
	READINGKEYS_NEXT
} readingkeys_action_t;

/* An action plus the reader session that owns it; fits in a 32-bit GUI post. */
typedef uint32_t readingkeys_event_t;
typedef struct {
	readingkeys_event_t held[KEYMAP_BTN_COUNT];
} readingkeys_tracker_t;

/* Config access: UI thread only. Volume buttons retain their normal mapping
 * by default. Enabling page turns only affects the physical case buttons. */
readingkeys_mode_t readingkeys_mode(void);
void readingkeys_set_mode(readingkeys_mode_t mode);

/* UI-thread writer; input threads only read atomic snapshots. Entering,
 * leaving or changing the mode invalidates old presses and queued actions. */
void readingkeys_enter(readingkeys_mode_t mode);
void readingkeys_leave(void);
bool readingkeys_active(void);
uint32_t readingkeys_epoch(void);

/* One tracker per input node/thread. value is kernel press=1, repeat=2,
 * release=0. Claim case media keys before music remapping/double-click logic.
 * Exactly one action is emitted on release, never during a hold. A consumed
 * release stays consumed if its reader session ended. Combinations suppress
 * the action without leaking it to music. Headset keys are not claimed.
 */
bool readingkeys_handle(readingkeys_tracker_t *tracker, keymap_button_t button, int value,
						bool headset, bool combination, readingkeys_event_t *event);

/* Validate again on GUI delivery; NONE means an old/invalid event, to drop.
 * Never fall back to a music action for an invalid reading event. */
readingkeys_action_t readingkeys_action(readingkeys_event_t event);

#endif
