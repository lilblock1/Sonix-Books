#include "src/system/device/nativegame_timing.h"
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
    puts("Native game cap and stereo PCM timing passed");
    return 0;
}
