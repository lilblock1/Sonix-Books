#include "powersettings.h"

#include "lvgl/lvgl.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/device/axpcharge.h"
#include "src/system/device/led.h"
#include "src/system/device/power.h"
#include "src/system/device/system.h"
#include <stdio.h>
#include <string.h>

lv_obj_t *powersettings_screen;

// 80 to 100 in fives. Stopping short of full is what keeps a lithium cell
// healthy if the device mostly lives on the charger.
#define CHARGE_LIMIT_MIN 80
#define CHARGE_LIMIT_STEP 5
#define CHARGE_LIMIT_COUNT 5 // 80, 85, 90, 95, 100

static const struct {
	int minutes;
	const char *label;
} AUTO_OFF[] = {
	{15, "power_15_minutes"}, {30, "power_30_minutes"}, {60, "power_1_hour"}, {120, "power_2_hours"}, {180, "power_3_hours"}, {360, "power_6_hours"},
};
#define AUTO_OFF_COUNT ((int)(sizeof(AUTO_OFF) / sizeof(AUTO_OFF[0])))

// How long the screen stays dark before standby. Saved in seconds under
// [power] standby_mem_seconds, which power.c reads at startup.
static const struct {
	int seconds;
	const char *label;
} STANDBY_AFTER[] = {
	{60, "power_1_minute"}, {180, "power_3_minutes"}, {300, "power_5_minutes"}, {600, "power_10_minutes"},
};
#define STANDBY_AFTER_COUNT ((int)(sizeof(STANDBY_AFTER) / sizeof(STANDBY_AFTER[0])))

// The standby card while standby is off: as low as a toggle with pills under
// it when they are folded away ("Split artists" in the music settings), the
// switch and the card's padding and nothing else.
#define STANDBY_CLOSED_H 76

static lv_obj_t *charge_value, *charge_slider;
static lv_obj_t *auto_off_switch, *auto_off_value, *auto_off_slider, *auto_off_card;
static lv_obj_t *led_on_switch, *led_off_switch, *led_off_card;
static lv_obj_t *key_lock_switch;
static lv_obj_t *adaptive_refresh_switch, *reading_dim_switch, *instant_turn_switch;
static lv_obj_t *profile_value, *profile_slider, *auto_saver_switch, *battery_diag_label;
static lv_timer_t *battery_diag_timer;
static const char *const PROFILE_NAMES[POWER_PROFILE_COUNT] = {"Custom", "Performance", "Balanced", "Battery Saver", "Ultra Saver"};
static power_profile_t selected_profile(void) {
	return power_profile_valid(config_get_int("power", "profile", POWER_CUSTOM));
}
static lv_obj_t *standby_switch, *standby_value, *standby_slider, *standby_card;
static lv_obj_t *charge_note;
static lv_obj_t *axp_limit_switch, *axp_current_switch; // the R1's two, instead of the slider

// --- reading the saved values ---

static int charge_index(void) {
	int percent = (int)config_get_int("power", "charge_limit", 100);
	int index = (percent - CHARGE_LIMIT_MIN) / CHARGE_LIMIT_STEP;
	if (index < 0) {
		index = 0;
	}
	if (index >= CHARGE_LIMIT_COUNT) {
		index = CHARGE_LIMIT_COUNT - 1;
	}
	return index;
}

static int auto_off_index(void) {
	int minutes = (int)config_get_int("power", "auto_off_minutes", 30);
	for (int i = 0; i < AUTO_OFF_COUNT; i++) {
		if (AUTO_OFF[i].minutes == minutes) {
			return i;
		}
	}
	return 1;
}

// A wait saved by hand that is not one of the four lands on the next one up,
// or on the longest.
static int standby_after_index(void) {
	int seconds = (int)config_get_int("power", "standby_mem_seconds", STANDBY_AFTER[0].seconds);
	for (int i = 0; i < STANDBY_AFTER_COUNT; i++) {
		if (seconds <= STANDBY_AFTER[i].seconds) {
			return i;
		}
	}
	return STANDBY_AFTER_COUNT - 1;
}

// --- applying them ---

static void set_checked(lv_obj_t *sw, bool on) {
	if (on) {
		lv_obj_add_state(sw, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(sw, LV_STATE_CHECKED);
	}
}

void powersettings_apply(void) {
	if (axpcharge_applies()) {
		// The R1 has no percentage slider: a percentage limit saved on it
		// becomes the 80 % switch.
		if (config_get_int("power", "charge_limit", 100) < 100) {
			config_set_int("power", "charge_limit", 100);
			config_set_bool("power", "charge_limit_80", true);
			config_save();
		}
		power_set_charge_limit(100);
		axpcharge_set(config_get_bool("power", "charge_limit_80", false),
					  config_get_bool("power", "charge_500ma", false));
	} else {
		power_set_charge_limit(CHARGE_LIMIT_MIN + (charge_index() * CHARGE_LIMIT_STEP));
	}

	power_set_auto_off(config_get_bool("power", "auto_off", false), (uint32_t)AUTO_OFF[auto_off_index()].minutes);

	// Suspend-to-RAM standby, on by default: saves battery by suspending the
	// player shortly after the screen goes off.
	power_set_standby_enabled(config_get_bool("power", "standby_mem", true));
	power_set_standby_after((uint32_t)STANDBY_AFTER[standby_after_index()].seconds);

	// The LED master switch comes first: with it off there is nothing for the
	// standby option to turn off, and led.c keeps the charge indicator alive
	// on its own regardless.
	led_set_enabled(config_get_bool("power", "led_on", true));
	led_set_idle_off(config_get_bool("power", "led_off_standby", true));

	input_set_key_lock(config_get_bool("power", "lock_keys_screen_off", false));
	power_set_display_options(config_get_bool("power", "adaptive_refresh", true),
							  config_get_bool("power", "reading_dim", false),
							  config_get_bool("power", "instant_reading_turns", false));
	power_set_profile(selected_profile(), config_get_bool("power", "auto_battery_saver", false));
}

static void refresh_labels(void) {
	if (charge_value) {
		lv_label_set_text_fmt(charge_value, "%d%%", CHARGE_LIMIT_MIN + (charge_index() * CHARGE_LIMIT_STEP));
	}
	if (axp_limit_switch) {
		set_checked(axp_limit_switch, config_get_bool("power", "charge_limit_80", false));
		set_checked(axp_current_switch, config_get_bool("power", "charge_500ma", false));
	}
	lv_label_set_text(auto_off_value, tr(AUTO_OFF[auto_off_index()].label));

	// The LED master switch, and the standby option that hangs off it: with the
	// LED switched off entirely there is nothing left for the standby option to
	// do, so it greys out.
	bool led_on = config_get_bool("power", "led_on", true);
	if (led_on) {
		lv_obj_add_state(led_on_switch, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(led_on_switch, LV_STATE_CHECKED);
	}

	if (config_get_bool("power", "led_off_standby", true)) {
		lv_obj_add_state(led_off_switch, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(led_off_switch, LV_STATE_CHECKED);
	}

	if (led_on) {
		lv_obj_remove_state(led_off_switch, LV_STATE_DISABLED);
		lv_obj_set_style_opa(led_off_card, LV_OPA_COVER, 0);
	} else {
		lv_obj_add_state(led_off_switch, LV_STATE_DISABLED);
		lv_obj_set_style_opa(led_off_card, LV_OPA_40, 0);
	}

	set_checked(key_lock_switch, config_get_bool("power", "lock_keys_screen_off", false));
	power_profile_t mode = selected_profile();
	lv_label_set_text(profile_value, PROFILE_NAMES[mode]);
	lv_slider_set_value(profile_slider, mode, LV_ANIM_OFF);
	set_checked(auto_saver_switch, config_get_bool("power", "auto_battery_saver", false));
	power_preset_t preset = power_preset(mode, config_get_bool("power", "adaptive_refresh", true),
		config_get_bool("power", "reading_dim", false), config_get_bool("power", "instant_reading_turns", false));
	set_checked(adaptive_refresh_switch, preset.adaptive);
	set_checked(reading_dim_switch, preset.dim);
	set_checked(instant_turn_switch, preset.instant);

	// Standby and its wait, on one card like the automatic power-off below.
	bool standby = config_get_bool("power", "standby_mem", true);
	if (standby) {
		lv_obj_add_state(standby_switch, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(standby_switch, LV_STATE_CHECKED);
	}
	lv_label_set_text(standby_value, tr(STANDBY_AFTER[standby_after_index()].label));
	settingsrow_toggle_slider_expanded(standby_card, standby);
	if (!standby) {
		lv_obj_set_height(standby_card, STANDBY_CLOSED_H);
	}

	// One card: the slider is only on it while the toggle is on.
	bool auto_off = config_get_bool("power", "auto_off", false);
	if (auto_off) {
		lv_obj_add_state(auto_off_switch, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(auto_off_switch, LV_STATE_CHECKED);
	}
	settingsrow_toggle_slider_expanded(auto_off_card, auto_off);
}

static void charge_changed_cb(lv_event_t *e) {
	(void)e;
	int index = (int)lv_slider_get_value(charge_slider);
	config_set_int("power", "charge_limit", CHARGE_LIMIT_MIN + (index * CHARGE_LIMIT_STEP));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void axp_limit_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "charge_limit_80", lv_obj_has_state(axp_limit_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void axp_current_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "charge_500ma", lv_obj_has_state(axp_current_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void auto_off_changed_cb(lv_event_t *e) {
	(void)e;
	int index = (int)lv_slider_get_value(auto_off_slider);
	config_set_int("power", "auto_off_minutes", AUTO_OFF[index].minutes);
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void led_off_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "led_off_standby", lv_obj_has_state(led_off_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

// The master switch. With the LED off the standby option has nothing left to
// do, so it greys out -- and the charger still lights the LED regardless, so
// a charge in progress is never invisible.
static void led_on_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "led_on", lv_obj_has_state(led_on_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void key_lock_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "lock_keys_screen_off", lv_obj_has_state(key_lock_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void auto_off_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "auto_off", lv_obj_has_state(auto_off_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void standby_after_changed_cb(lv_event_t *e) {
	(void)e;
	int index = (int)lv_slider_get_value(standby_slider);
	config_set_int("power", "standby_mem_seconds", STANDBY_AFTER[index].seconds);
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void standby_toggled_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "standby_mem", lv_obj_has_state(standby_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void display_saver_changed_cb(lv_event_t *e) {
	(void)e;
	// Changing an individual option copies the shown settings into Custom.
	config_set_int("power", "profile", POWER_CUSTOM);
	config_set_bool("power", "adaptive_refresh", lv_obj_has_state(adaptive_refresh_switch, LV_STATE_CHECKED));
	config_set_bool("power", "reading_dim", lv_obj_has_state(reading_dim_switch, LV_STATE_CHECKED));
	config_set_bool("power", "instant_reading_turns", lv_obj_has_state(instant_turn_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void profile_changed_cb(lv_event_t *e) {
	(void)e;
	config_set_int("power", "profile", power_profile_valid(lv_slider_get_value(profile_slider)));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void auto_saver_changed_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("power", "auto_battery_saver", lv_obj_has_state(auto_saver_switch, LV_STATE_CHECKED));
	config_save();
	powersettings_apply();
	refresh_labels();
}

static void battery_diag_refresh(void) {
	if (!battery_diag_label || !power_screen_is_on()) return;
	power_diagnostics_t d;
	power_get_diagnostics(&d);
	char capacity[24] = "Unavailable", voltage[32] = "Unavailable", drain[48] = "Collecting data", remaining[48] = "Unavailable";
	if (d.percent >= 0) snprintf(capacity, sizeof(capacity), "%d%%", d.percent);
	if (d.millivolts > 0) snprintf(voltage, sizeof(voltage), "%d mV", d.millivolts);
	if (d.plugged) snprintf(drain, sizeof(drain), "External power");
	else if (d.drain_known) {
		snprintf(drain, sizeof(drain), "%u.%u%% / h", d.drain_tenths_per_hour / 10, d.drain_tenths_per_hour % 10);
		snprintf(remaining, sizeof(remaining), "~%u h %u min", d.estimated_minutes / 60, d.estimated_minutes % 60);
	}
	char report[512];
	snprintf(report, sizeof(report),
		"Battery: %s\nVoltage: %s\nPower: %s\nDrain: %s\nEstimated remaining: %s\nScreen-on since boot: %u h %u min\nEffective mode: %s\nWi-Fi: %s | BT: %s\nRadio parked: %s",
		capacity, voltage, d.charging ? "Charging" : d.plugged ? "Plugged in" : d.percent >= 0 ? "On battery" : "Unknown",
		drain, remaining, d.screen_on_seconds / 3600, d.screen_on_seconds / 60 % 60,
		PROFILE_NAMES[power_profile_valid(power_get_effective_profile())],
		d.wifi_on ? "On" : "Off", d.bluetooth_on ? "On" : "Off", d.parked ? "Yes" : "No");
	if (strcmp(lv_label_get_text(battery_diag_label), report)) lv_label_set_text(battery_diag_label, report);
}

static void battery_diag_cb(lv_timer_t *timer) {
	if (lv_screen_active() != powersettings_screen) { lv_timer_pause(timer); return; }
	battery_diag_refresh();
}

static void power_page_loaded(lv_event_t *e) {
	(void)e;
	refresh_labels();
	battery_diag_refresh();
	if (battery_diag_timer) { lv_timer_reset(battery_diag_timer); lv_timer_resume(battery_diag_timer); }
}

static void power_page_unloaded(lv_event_t *e) {
	(void)e;
	if (battery_diag_timer) lv_timer_pause(battery_diag_timer);
}

void powersettings_init(gui_config_t *cfg) {
	lv_obj_t *container = settingsrow_page(powersettings_screen, cfg, "power");

	settingsrow_slider(container, "Power mode", POWER_PROFILE_COUNT, &profile_value, &profile_slider, profile_changed_cb);
	settingsrow_toggle(container, "Auto saver at 20%", &auto_saver_switch, auto_saver_changed_cb);
	// English fallback labels until the translated power-profile strings land.
	settingsrow_toggle(container, "Adaptive UI refresh", &adaptive_refresh_switch, display_saver_changed_cb);
	settingsrow_toggle(container, "Dim reading after 3 min", &reading_dim_switch, display_saver_changed_cb);
	settingsrow_toggle(container, "Instant reading page turns", &instant_turn_switch, display_saver_changed_cb);

	// Suspend-to-RAM standby: suspends the player a while after the screen goes
	// off to save battery, and playback resumes on wake. On by default. What it
	// waits for is the screen going dark, and that timeout lives on the Screen
	// page, beside the brightness it belongs with; how long it then waits is
	// the slider on this card, there only while standby is on.
	standby_card = settingsrow_toggle_slider(container, "power_standby_mem", STANDBY_AFTER_COUNT, &standby_switch,
											 &standby_value, &standby_slider, standby_toggled_cb,
											 standby_after_changed_cb);
	lv_slider_set_value(standby_slider, standby_after_index(), LV_ANIM_OFF);
	lv_obj_t *standby_note = lv_label_create(container);
	lv_label_set_long_mode(standby_note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(standby_note, lv_pct(100));
	lv_obj_add_style(standby_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(standby_note, &font_ui_22, 0);
	lv_label_set_text(standby_note, tr("power_standby_mem_note"));

	// The R1 charges through its PMIC, which takes a lower target voltage and a
	// lower current rather than a percentage: two switches there, the slider
	// everywhere else.
	bool pmic = axpcharge_applies();
	if (pmic) {
		settingsrow_toggle(container, "power_charge_limit_80", &axp_limit_switch, axp_limit_toggled_cb);
		lv_obj_t *limit_note = lv_label_create(container);
		lv_label_set_long_mode(limit_note, LV_LABEL_LONG_WRAP);
		lv_obj_set_width(limit_note, lv_pct(100));
		lv_obj_add_style(limit_note, &theme_style_text_dim, 0);
		lv_obj_set_style_text_font(limit_note, &font_ui_22, 0);
		lv_label_set_text(limit_note, tr("power_charge_limit_80_note"));
		settingsrow_toggle(container, "power_charge_500ma", &axp_current_switch, axp_current_toggled_cb);
	} else {
		settingsrow_slider(container, "power_charge_limit", CHARGE_LIMIT_COUNT, &charge_value, &charge_slider,
						   charge_changed_cb);
		lv_slider_set_value(charge_slider, charge_index(), LV_ANIM_OFF);
	}

	// The LED master switch, and the standby option that depends on it.
	settingsrow_toggle(container, "power_led_on", &led_on_switch, led_on_toggled_cb);
	led_off_card = settingsrow_toggle(container, "power_led_off_in_standby", &led_off_switch, led_off_toggled_cb);
	// No "turn Wi-Fi off in standby" entry: parking the radio is automatic, and
	// wifi_in_use() in power.c knows on its own when not to turn it off --
	// Qobuz, Tidal, podcasts, radio, AirPlay, DLNA, transfers.

	// The lock gesture for the buttons on the case (system.c).
	settingsrow_toggle(container, "power_lock_keys_screen_off", &key_lock_switch, key_lock_toggled_cb);
	lv_obj_t *key_lock_note = lv_label_create(container);
	lv_label_set_long_mode(key_lock_note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(key_lock_note, lv_pct(100));
	lv_obj_add_style(key_lock_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(key_lock_note, &font_ui_22, 0);
	lv_label_set_text(key_lock_note, tr("power_lock_keys_screen_off_note"));

	// Toggle and "after how long" on one card: the slider is only there while
	// the option is on.
	auto_off_card = settingsrow_toggle_slider(container, "power_auto_power_off", AUTO_OFF_COUNT,
											  &auto_off_switch, &auto_off_value, &auto_off_slider,
											  auto_off_toggled_cb, auto_off_changed_cb);
	lv_slider_set_value(auto_off_slider, auto_off_index(), LV_ANIM_OFF);

	// If the charger driver has no control node there is nothing to enforce
	// the limit with, and saying so is better than a setting that silently
	// does nothing.
	charge_note = lv_label_create(container);
	lv_label_set_long_mode(charge_note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(charge_note, lv_pct(100));
	lv_obj_add_style(charge_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(charge_note, &font_ui_22, 0);
	lv_label_set_text(charge_note, pmic || power_charge_limit_supported() ? tr("power_auto_off_note")
																	   : tr("power_charge_limit_unavailable"));

 	// Read-only session diagnostics. No extra sampling timer while hidden.
	battery_diag_label = lv_label_create(container);
	lv_obj_set_width(battery_diag_label, lv_pct(100));
	lv_label_set_long_mode(battery_diag_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(battery_diag_label, &font_ui_20, 0);
	lv_label_set_text(battery_diag_label, "Battery diagnostics (estimates vary with use)");
	battery_diag_timer = lv_timer_create(battery_diag_cb, 5000, NULL);
	if (battery_diag_timer) { lv_timer_pause(battery_diag_timer); power_pause_in_standby(battery_diag_timer); }
	lv_obj_add_event_cb(powersettings_screen, power_page_loaded, LV_EVENT_SCREEN_LOADED, NULL);
	lv_obj_add_event_cb(powersettings_screen, power_page_unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);

	// Everything saved becomes effective now, at startup -- including the LED
	// standby option, which lives in led.c rather than power.c.
	powersettings_apply();
	refresh_labels();
}
