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
// Keep the DAC at a standard 48 kHz even when the core reports 65536 Hz
// (gpSP) or the user lowers emulation speed. Continuous stereo linear
// interpolation preserves the sample-clock ratio across callback boundaries.
#define GAME_PCM_RATE 48000u
typedef struct {
    unsigned in_rate, out_rate, phase;
    int16_t left,right;
    bool ready;
} game_resampler_t;
typedef void (*game_pcm_emit_t)(int16_t,int16_t);
static inline void game_resampler_reset(game_resampler_t *r,unsigned in_rate,unsigned out_rate) {
    *r=(game_resampler_t){.in_rate=in_rate,.out_rate=out_rate};
}
static inline void game_resampler_push(game_resampler_t *r,int16_t left,int16_t right,game_pcm_emit_t emit) {
    if(!r->in_rate||!r->out_rate)return;
    if(r->in_rate==r->out_rate){emit(left,right);return;}
    if(!r->ready){r->left=left;r->right=right;r->ready=true;return;}
    while(r->phase<r->out_rate){
        // Q15 interpolation: signed products fit int32 even at full scale.
        int32_t frac=(int32_t)(r->phase*32768u/r->out_rate);
        int32_t l=r->left+((int32_t)left-r->left)*frac/32768;
        int32_t rr=r->right+((int32_t)right-r->right)*frac/32768;
        emit((int16_t)l,(int16_t)rr);
        r->phase+=r->in_rate;
    }
    r->phase-=r->out_rate;
    r->left=left;r->right=right;
}
#endif
