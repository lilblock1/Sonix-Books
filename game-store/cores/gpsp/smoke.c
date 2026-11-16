/* gpSP frontend smoke test, same minimal environment as Sonix nativegame. */
#include "libretro.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned frames,audio,polls,reads,red_frames;
static bool env(unsigned cmd,void *data){
	if(!data)return false;
	switch(cmd){
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data=true;return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(unsigned*)data==RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_GET_VARIABLE:((struct retro_variable*)data)->value=NULL;return false;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=".";return true;
	default:return false;
	}
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch){
	if(!p)return;
	assert(w==240&&h==160&&pitch>=w*2);++frames;
	uint16_t pixel;memcpy(&pixel,p,sizeof(pixel));if(pixel==0xf800)++red_frames;
}
static void sample(int16_t l,int16_t r){(void)l;(void)r;++audio;}
static size_t batch(const int16_t *p,size_t n){assert(p||!n);audio+=(unsigned)n;return n;}
static void poll(void){++polls;}
static int16_t input(unsigned port,unsigned dev,unsigned index,unsigned id){(void)port;(void)dev;(void)index;(void)id;++reads;return 0;}
#define LOAD(ret,name,params) ret (*name) params=(ret (*) params)dlsym(core,#name);assert(name)
int main(int argc,char **argv){
	assert(argc==3);void *core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
	if(!core){fprintf(stderr,"%s\n",dlerror());return 1;}
	LOAD(unsigned,retro_api_version,(void));
	LOAD(void,retro_set_environment,(retro_environment_t));
	LOAD(void,retro_set_video_refresh,(retro_video_refresh_t));
	LOAD(void,retro_set_audio_sample,(retro_audio_sample_t));
	LOAD(void,retro_set_audio_sample_batch,(retro_audio_sample_batch_t));
	LOAD(void,retro_set_input_poll,(retro_input_poll_t));
	LOAD(void,retro_set_input_state,(retro_input_state_t));
	LOAD(void,retro_init,(void));LOAD(void,retro_deinit,(void));
	LOAD(bool,retro_load_game,(const struct retro_game_info*));
	LOAD(void,retro_unload_game,(void));LOAD(void,retro_run,(void));
	LOAD(void,retro_get_system_info,(struct retro_system_info*));
	LOAD(void,retro_get_system_av_info,(struct retro_system_av_info*));
	LOAD(size_t,retro_serialize_size,(void));LOAD(bool,retro_serialize,(void*,size_t));
	LOAD(bool,retro_unserialize,(const void*,size_t));
	LOAD(void*,retro_get_memory_data,(unsigned));LOAD(size_t,retro_get_memory_size,(unsigned));
	assert(retro_api_version()==1);
	retro_set_environment(env);retro_set_video_refresh(video);retro_set_audio_sample(sample);
	retro_set_audio_sample_batch(batch);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
	struct retro_system_info si={0};retro_get_system_info(&si);assert(si.need_fullpath&&strstr(si.valid_extensions,"gba"));
	struct retro_game_info info={.path=argv[2]};
	assert(retro_load_game(&info));
	struct retro_system_av_info av={0};retro_get_system_av_info(&av);assert(av.timing.fps>55&&av.timing.fps<65&&av.timing.sample_rate>8000);
	for(unsigned i=0;i<120;++i)retro_run();
	assert(frames>100&&audio>1000&&polls>100&&reads>0);
	assert(red_frames>0);
	size_t n=retro_serialize_size();assert(n>0&&n<=2*1024*1024);
	void *state=malloc(n);assert(state&&retro_serialize(state,n)&&retro_unserialize(state,n));free(state);
	assert(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)&&retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)==0x20000);
	retro_unload_game();retro_deinit();dlclose(core);
	printf("gpSP smoke passed: %u frames, %u red-pixel frames, %u audio sample frames, %u input reads, %zu-byte save state\n",frames,red_frames,audio,reads,n);
	return 0;
}
