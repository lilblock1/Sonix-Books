#include <assert.h>
#include <limits.h>
#include "src/system/device/power_profile.h"

int main(void) {
    assert(power_profile_valid(-1) == POWER_CUSTOM);
    assert(power_profile_valid(LONG_MAX) == POWER_CUSTOM);
    for (int i = POWER_CUSTOM; i < POWER_PROFILE_COUNT; ++i) {
        power_profile_t m = (power_profile_t)i;
        power_preset_t p = power_preset(m, true, false, false);
        assert(power_profile_valid(i) == m);
        assert(p.reader_ms >= 16 && p.reader_ms <= 200);
        assert(p.home_ms >= 16 && p.home_ms <= 200);
        assert(p.reader_wifi_seconds == 0 ||
               (p.reader_wifi_seconds >= 30 && p.reader_wifi_seconds <= 120));
        assert(p.dim_seconds >= 120);
        assert(power_profile_effective(m, false, false, 5, false) == m);
        assert(power_profile_effective(m, true, true, 5, true) == m);
        assert(power_profile_effective(m, true, false, -1, true) == m);
        assert(power_profile_effective(m, true, false, 101, true) == m);
    }
    assert(power_profile_effective(POWER_PERFORMANCE, true, false, 20, false) == POWER_SAVER);
    assert(power_profile_effective(POWER_BALANCED, true, false, 21, false) == POWER_BALANCED);
    assert(power_profile_effective(POWER_BALANCED, true, false, 24, true) == POWER_SAVER);
    assert(power_profile_effective(POWER_BALANCED, true, false, 25, true) == POWER_BALANCED);
    assert(power_profile_effective(POWER_ULTRA, true, false, 10, false) == POWER_ULTRA);
    power_preset_t p = power_preset(POWER_PERFORMANCE, true, true, true);
    assert(!p.adaptive && !p.dim && !p.instant && p.reader_wifi_seconds == 0);
    p = power_preset(POWER_CUSTOM, false, true, true);
    assert(!p.adaptive && p.dim && p.instant);
    p = power_preset(POWER_ULTRA, false, false, false);
    assert(p.adaptive && p.dim && p.instant && p.reader_wifi_seconds == 30);
    return 0;
}
