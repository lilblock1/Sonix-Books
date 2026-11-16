#ifndef NATIVEGAME_OPTIONS_H
#define NATIVEGAME_OPTIONS_H
#include "nativegame_timing.h"
// Serialized as fixed-width bytes, independent of machine ABI.
// cap remains independent of render skip and preset.
enum { GAME_SKIP_OFF, GAME_SKIP_ADAPTIVE, GAME_SKIP_HALF };
enum { GAME_LAYOUT_STANDARD, GAME_LAYOUT_MIRRORED };
enum { GAME_JIT_DEFAULT, GAME_JIT_INTERPRETER };
enum { GAME_PRESET_BATTERY, GAME_PRESET_BALANCED, GAME_PRESET_PERFORMANCE };
typedef struct {
    uint8_t version, fps, skip, buffer_ms, overlay, jit, slot, layout;
} game_options_t;
static inline game_options_t game_options_default(void) {
    return (game_options_t){1,0,GAME_SKIP_OFF,64,0,GAME_JIT_DEFAULT,0,GAME_LAYOUT_STANDARD};
}
static inline bool game_options_valid(game_options_t v) {
    return v.version==1&&game_fps_valid(v.fps)&&v.skip<=GAME_SKIP_HALF&&
        (v.buffer_ms==40||v.buffer_ms==64||v.buffer_ms==96||v.buffer_ms==128)&&
        v.overlay<=1&&v.jit<=GAME_JIT_INTERPRETER&&v.slot<=3&&v.layout<=GAME_LAYOUT_MIRRORED;
}
static inline void game_options_preset(game_options_t *v,unsigned preset) {
    // These reduce video/PCM overhead without changing CPU clocks or core speed.
    if(preset==GAME_PRESET_BATTERY){v->skip=GAME_SKIP_HALF;v->buffer_ms=96;}
    else if(preset==GAME_PRESET_BALANCED){v->skip=GAME_SKIP_ADAPTIVE;v->buffer_ms=64;}
    else if(preset==GAME_PRESET_PERFORMANCE){v->skip=GAME_SKIP_ADAPTIVE;v->buffer_ms=96;}
}
static inline bool game_should_skip(unsigned mode,bool previous_skip,bool overloaded) {
    // Never deliberately drop two consecutive emulated frames.
    return !previous_skip&&(mode==GAME_SKIP_HALF||(mode==GAME_SKIP_ADAPTIVE&&overloaded));
}
#endif
