#ifndef NATIVEGAME_TIMING_H
#define NATIVEGAME_TIMING_H
#include <stdbool.h>
#include <stdint.h>
// Zero means native speed. Never run faster than the core's own refresh rate.
#define NATIVEGAME_FPS_MIN 30u
#define NATIVEGAME_FPS_MAX 120u
static inline bool game_fps_valid(unsigned fps) {
    return fps==0 || (fps>=NATIVEGAME_FPS_MIN && fps<=NATIVEGAME_FPS_MAX);
}
static inline double game_effective_fps(double native, unsigned limit) {
    return limit && (double)limit<native ? (double)limit : native;
}
static inline int64_t game_period_ns(double native, unsigned limit) {
    // Round up so the requested cap is not exceeded by rounding.
    double ns=1000000000.0/game_effective_fps(native,limit);
    int64_t whole=(int64_t)ns;
    return whole+(ns>(double)whole);
}
static inline int game_audio_rate(double native,double rate,unsigned limit) {
    // Slow the sample clock with emulation. Pitch changes intentionally.
    double scaled=rate*game_effective_fps(native,limit)/native;
    return (int)(scaled+0.5);
}
#endif
