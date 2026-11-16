#include "src/system/device/nativegame_options.h"
#include <assert.h>
#include <stdio.h>
static unsigned count;
static int16_t last_l,last_r;
static void emit(int16_t l,int16_t r){++count;last_l=l;last_r=r;assert(l>=-30000&&r<=30000);}
int main(void){
    assert(game_fps_valid(0)&&game_fps_valid(30)&&game_fps_valid(120));
    assert(!game_fps_valid(1)&&!game_fps_valid(121));
    const double gba=59.7275;
    assert(game_effective_fps(gba,0)==gba&&game_effective_fps(gba,60)==gba);
    assert(game_effective_fps(gba,45)==45&&game_period_ns(gba,45)==22222223);
    assert(game_period_ns(60,60)==16666667);
    assert(game_audio_rate(60,48000,30)==24000);
    assert(game_audio_rate(gba,65536,0)==65536);
    game_resampler_t r;
    game_resampler_reset(&r,65536,48000);count=0;
    for(unsigned i=0;i<=65536;i++)game_resampler_push(&r,-1200,2300,emit);
    assert(count==48000&&last_l==-1200&&last_r==2300);
    game_resampler_reset(&r,24000,48000);count=0;
    for(unsigned i=0;i<=24000;i++)game_resampler_push(&r,-2800,6400,emit);
    assert(count==48000&&last_l==-2800&&last_r==6400);
    game_resampler_reset(&r,48000,48000);count=0;
    game_resampler_push(&r,123,-456,emit);
    assert(count==1&&last_l==123&&last_r==-456);
    game_resampler_reset(&r,24000,48000);count=0;
    game_resampler_push(&r,-1000,1000,emit);game_resampler_push(&r,1000,-1000,emit);
    assert(count==2&&last_l==0&&last_r==0);
    _Static_assert(sizeof(game_options_t)==8,"persisted options must remain eight bytes");
    game_options_t opt=game_options_default();
    assert(game_options_valid(opt)&&opt.skip==GAME_SKIP_OFF);
    opt.fps=45;opt.overlay=1;opt.jit=GAME_JIT_INTERPRETER;opt.slot=3;opt.layout=GAME_LAYOUT_MIRRORED;
    game_options_preset(&opt,GAME_PRESET_BATTERY);
    assert(game_options_valid(opt)&&opt.skip==GAME_SKIP_HALF&&opt.buffer_ms==96);
    assert(opt.fps==45&&opt.overlay==1&&opt.jit==GAME_JIT_INTERPRETER&&opt.slot==3&&opt.layout==GAME_LAYOUT_MIRRORED);
    game_options_preset(&opt,GAME_PRESET_BALANCED);
    assert(opt.skip==GAME_SKIP_ADAPTIVE&&opt.buffer_ms==64);
    game_options_preset(&opt,GAME_PRESET_PERFORMANCE);
    assert(opt.skip==GAME_SKIP_ADAPTIVE&&opt.buffer_ms==96);
    game_options_t broken=opt;broken.buffer_ms=0;assert(!game_options_valid(broken));
    broken=opt;broken.slot=4;assert(!game_options_valid(broken));
    broken=opt;broken.jit=2;assert(!game_options_valid(broken));
    broken=opt;broken.version=2;assert(!game_options_valid(broken));
    bool previous=false;unsigned skipped=0;
    for(unsigned i=0;i<120;++i){
        bool skip=game_should_skip(GAME_SKIP_ADAPTIVE,previous,true);
        assert(!(skip&&previous));skipped+=skip;previous=skip;
    }
    assert(skipped==60);
    assert(!game_should_skip(GAME_SKIP_ADAPTIVE,false,false));
    assert(!game_should_skip(GAME_SKIP_OFF,false,true));
    assert(game_should_skip(GAME_SKIP_HALF,false,false));
    assert(!game_should_skip(GAME_SKIP_HALF,true,false));
    puts("Native game cap, PCM, presets and bounded display skip passed");
    return 0;
}
