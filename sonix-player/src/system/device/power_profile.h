#ifndef SONIX_POWER_PROFILE_H
#define SONIX_POWER_PROFILE_H
#include <stdbool.h>

typedef enum { POWER_CUSTOM, POWER_PERFORMANCE, POWER_BALANCED, POWER_SAVER, POWER_ULTRA, POWER_PROFILE_COUNT } power_profile_t;
typedef struct {
    bool adaptive, dim, instant;
    unsigned reader_ms, home_ms, reader_wifi_seconds, dim_seconds;
} power_preset_t;

static inline power_profile_t power_profile_valid(long n) {
    return n >= POWER_CUSTOM && n < POWER_PROFILE_COUNT ? (power_profile_t)n : POWER_CUSTOM;
}
static inline power_profile_t power_profile_effective(power_profile_t selected, bool automatic, bool plugged, int percent, bool previously_auto) {
    if (automatic && !plugged && percent >= 0 && percent <= 100 &&
        (percent <= 20 || (previously_auto && percent < 25)) &&
        selected != POWER_SAVER && selected != POWER_ULTRA) return POWER_SAVER;
    return selected;
}
static inline power_preset_t power_preset(power_profile_t mode, bool adaptive, bool dim, bool instant) {
    switch (mode) {
    case POWER_PERFORMANCE: return (power_preset_t){false,false,false,100,67,0,180};
    case POWER_BALANCED:    return (power_preset_t){true,false,false,100,67,120,180};
    case POWER_SAVER:       return (power_preset_t){true,true,true,125,100,60,180};
    case POWER_ULTRA:       return (power_preset_t){true,true,true,200,133,30,120};
    default:               return (power_preset_t){adaptive,dim,instant,100,67,60,180};
    }
}
#endif
