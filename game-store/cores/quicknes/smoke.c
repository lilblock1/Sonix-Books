/* Original smoke test, no third-party ROM data. */
#include "libretro.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned frames,audio,polls,reads;
static uint64_t video_hash=1469598103934665603ULL,audio_hash=1469598103934665603ULL;
static bool full_overscan;
static uint64_t hash_bytes(uint64_t h,const void *data,size_t n){const uint8_t *p=data;while(n--){h^=*p++;h*=1099511628211ULL;}return h;}
static bool env(unsigned cmd,void *data) {
	if(cmd==RETRO_ENVIRONMENT_GET_CAN_DUPE&&data){*(bool*)data=true;return true;}
	if(cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT&&data)return *(unsigned*)data<=2;
	if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE&&data){*(bool*)data=false;return true;}
	if(cmd==RETRO_ENVIRONMENT_GET_VARIABLE&&data){struct retro_variable *v=data;
		if(full_overscan&&(!strcmp(v->key,"quicknes_use_overscan_h")||!strcmp(v->key,"quicknes_use_overscan_v"))){v->value="enabled";return true;}
		v->value=NULL;return false;}
	return false;
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch){if(p){assert(w>0&&w<=256&&h>0&&h<=240&&pitch>=w*2);++frames;for(unsigned y=0;y<h;++y)video_hash=hash_bytes(video_hash,(const uint8_t*)p+y*pitch,w*2);}}
static void sample(int16_t l,int16_t r){int16_t pair[2]={l,r};audio_hash=hash_bytes(audio_hash,pair,sizeof(pair));++audio;}
static size_t batch(const int16_t *p,size_t n){assert(p);audio+=(unsigned)n;audio_hash=hash_bytes(audio_hash,p,n*2*sizeof(*p));return n;}
static void poll(void){++polls;}
static int16_t input(unsigned port,unsigned dev,unsigned index,unsigned id){(void)port;(void)dev;(void)index;++reads;return id==RETRO_DEVICE_ID_JOYPAD_A;}
#define LOAD(ret,name,params) ret (*name) params = (ret (*) params)dlsym(core,#name);assert(name)
int main(int argc,char **argv){
	assert(argc==4);assert(!strcmp(argv[2],"crop")||!strcmp(argv[2],"full"));assert(!strcmp(argv[3],"tone")||!strcmp(argv[3],"picture"));
	full_overscan=!strcmp(argv[2],"full");bool picture_mode=!strcmp(argv[3],"picture");void *core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
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
	LOAD(void,retro_get_system_av_info,(struct retro_system_av_info*));
	LOAD(size_t,retro_serialize_size,(void));
	LOAD(bool,retro_serialize,(void*,size_t));LOAD(bool,retro_unserialize,(const void*,size_t));
	assert(retro_api_version()==1);
	retro_set_environment(env);retro_set_video_refresh(video);retro_set_audio_sample(sample);
	retro_set_audio_sample_batch(batch);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
	static uint8_t rom[16+16384+8192];
	memcpy(rom,"NES\x1a",4);rom[4]=1;rom[5]=1;memset(rom+16,0xea,16384);
	// SEI, CLD, LDX #$ff, TXS; enable APU and write pulse tone; infinite loop.
	const uint8_t code[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,0x01,0x8d,0x15,0x40,0xa9,0xbf,0x8d,0x00,0x40,0xa9,0xff,0x8d,0x02,0x40,0xa9,0x08,0x8d,0x03,0x40,0x4c,0x18,0x80};
	memcpy(rom+16,code,sizeof(code));for(unsigned i=0x3ffa;i<0x4000;i+=2){rom[16+i]=0;rom[16+i+1]=0x80;}
	if(picture_mode){
		// Wait for vblank twice, set background palette, reset PPU address and scroll.
		const uint8_t picture[]={0x78,0xd8,0xa2,0xff,0x9a,0x2c,0x02,0x20,0x10,0xfb,0x2c,0x02,0x20,0x10,0xfb,0xa9,0x3f,0x8d,0x06,0x20,0xa9,0x00,0x8d,0x06,0x20,0xa9,0x0f,0x8d,0x07,0x20,0xa9,0x21,0x8d,0x07,0x20,0xa9,0x16,0x8d,0x07,0x20,0xa9,0x30,0x8d,0x07,0x20,0xa9,0x20,0x8d,0x06,0x20,0xa9,0x00,0x8d,0x06,0x20,0x8d,0x05,0x20,0x8d,0x05,0x20,0xa9,0x0a,0x8d,0x01,0x20,0x4c,0x00,0x80};
		memset(rom+16,0xea,16384);memcpy(rom+16,picture,sizeof(picture));
		rom[16+sizeof(picture)-2]=(uint8_t)(sizeof(picture)-3);
		for(unsigned i=0x3ffa;i<0x4000;i+=2){rom[16+i]=0;rom[16+i+1]=0x80;}
		for(unsigned i=0;i<8;++i){rom[16+16384+i]=(i&1)?0xaa:0x55;rom[16+16384+i+8]=(i&2)?0xff:0;}
	}
	struct retro_game_info info={.path="self-test.nes",.data=rom,.size=sizeof(rom)};
	assert(retro_load_game(&info));struct retro_system_av_info av;retro_get_system_av_info(&av);assert(av.timing.fps>50&&av.timing.fps<65);
	for(unsigned i=0;i<120;++i)retro_run();
	assert(frames>100&&audio>1000&&polls>0&&reads>0);
	size_t n=retro_serialize_size();assert(n>0&&n<=2*1024*1024);
	void *state=malloc(n);assert(state&&retro_serialize(state,n)&&retro_unserialize(state,n));free(state);
	retro_unload_game();retro_deinit();dlclose(core);
	printf("QuickNES smoke passed: %u video frames, %u audio samples, %u input reads; RGB565=%016llx PCM=%016llx\n",frames,audio,reads,(unsigned long long)video_hash,(unsigned long long)audio_hash);return 0;
}
