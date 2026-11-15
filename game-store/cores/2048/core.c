/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original compact 2048-style native game for Sonix's planned libretro loader.
 * No game ROM, dependencies, heap allocations, or external assets.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define W 240
#define H 240
#define FPS 60.0
#define ENV_PIXEL_FORMAT 10
#define ENV_SUPPORT_NO_GAME 18
#define DEVICE_JOYPAD 1
#define PIXEL_RGB565 2

typedef bool (*env_cb)(unsigned,void*);
typedef void (*video_cb)(const void*,unsigned,unsigned,size_t);
typedef void (*audio_cb)(int16_t,int16_t);
typedef size_t (*audio_batch_cb)(const int16_t*,size_t);
typedef void (*poll_cb)(void);
typedef int16_t (*input_cb)(unsigned,unsigned,unsigned,unsigned);
struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct retro_system_info { const char *library_name,*library_version,*valid_extensions; bool need_fullpath,block_extract; };
struct retro_game_geometry { unsigned base_width,base_height,max_width,max_height; float aspect_ratio; };
struct retro_system_timing { double fps,sample_rate; };
struct retro_system_av_info { struct retro_game_geometry geometry; struct retro_system_timing timing; };

static env_cb environment;
static video_cb video;
static poll_cb input_poll;
static input_cb input_state;
static uint16_t frame[W*H];
typedef struct { uint32_t rng,score; uint8_t cells[16]; } game_state_t;
static game_state_t game;
static uint16_t previous_keys;
static bool dirty=true;

static uint32_t next_random(void) {
	uint32_t x=game.rng?game.rng:0x8852c9d1u;
	x^=x<<13; x^=x>>17; x^=x<<5;
	return game.rng=x;
}
static void spawn(void) {
	uint8_t empty[16], n=0;
	for(unsigned i=0;i<16;++i) if(!game.cells[i]) empty[n++]=(uint8_t)i;
	if(n) {
		unsigned pos=next_random()%n;
		game.cells[empty[pos]]=next_random()%10==0?2:1;
	}
}
static void reset(void) {
	uint32_t seed=next_random();
	memset(&game,0,sizeof(game)); game.rng=seed;
	spawn(); spawn(); previous_keys=0;dirty=true;
}
static unsigned index_at(unsigned direction,unsigned line,unsigned pos) {
	switch(direction) {
	case 0: return pos*4+line;
	case 1: return (3-pos)*4+line;
	case 2: return line*4+pos;
	default:return line*4+3-pos;
	}
}
static bool slide(unsigned direction) {
	bool changed=false;
	for(unsigned i=0;i<4;++i) {
		uint8_t compact[4]={0}, merged[4]={0};
		unsigned n=0,m=0;
		for(unsigned j=0;j<4;++j) {
			uint8_t v=game.cells[index_at(direction,i,j)];
			if(v) compact[n++]=v;
		}
		for(unsigned j=0;j<n;++j) {
			uint8_t v=compact[j];
			if(j+1<n && compact[j+1]==v && v<30) {
				++v; ++j;
				uint32_t points=1u<<v;
				game.score=UINT32_MAX-game.score<points?UINT32_MAX:game.score+points;
			}
			merged[m++]=v;
		}
		for(unsigned j=0;j<4;++j) {
			unsigned p=index_at(direction,i,j);
			if(game.cells[p]!=merged[j]) changed=true;
			game.cells[p]=merged[j];
		}
	}
	return changed;
}
static bool has_move(void) {
	for(unsigned i=0;i<16;++i) {
		if(!game.cells[i]) return true;
		if(i%4<3 && game.cells[i]==game.cells[i+1] && game.cells[i]<30) return true;
		if(i<12 && game.cells[i]==game.cells[i+4] && game.cells[i]<30) return true;
	}
	return false;
}
static uint16_t color(unsigned r,unsigned g,unsigned b) {
	return (uint16_t)(((r&248)<<8)|((g&252)<<3)|(b>>3));
}
static void rect(int x,int y,int w,int h,uint16_t c) {
	for(int j=y;j<y+h && j<H;++j) if(j>=0)
		for(int i=x;i<x+w && i<W;++i) if(i>=0) frame[j*W+i]=c;
}
static const uint16_t digit[10]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
static void number(uint32_t n,int cx,int y,int scale,uint16_t c) {
	uint8_t d[10]; unsigned count=0;
	do { d[count++]=(uint8_t)(n%10);n/=10; } while(n && count<10);
	int x=cx-((int)count*4*scale-scale)/2;
	for(unsigned i=count;i>0;--i) {
		uint16_t bits=digit[d[i-1]];
		for(int r=0;r<5;++r) for(int col=0;col<3;++col)
			if(bits&(1u<<(14-r*3-col))) rect(x+col*scale,y+r*scale,scale,scale,c);
		x+=4*scale;
	}
}
static void draw(void) {
	static const uint8_t palette[][3]={{78,72,68},{239,228,211},{237,222,189},{247,174,112},{246,143,86},{247,114,86},{240,84,55},{236,199,92},{236,187,71},{235,173,49},{225,154,23},{203,119,19}};
	rect(0,0,W,H,color(44,39,36));
	number(game.score,W/2,4,2,color(255,246,227));
	for(unsigned i=0;i<16;++i) {
		uint8_t v=game.cells[i], k=v>11?11:v;
		int x=6+(int)(i%4)*58,y=21+(int)(i/4)*54;
		rect(x,y,54,50,color(palette[k][0],palette[k][1],palette[k][2]));
		if(v) number(1u<<v,x+27,y+18,v>12?1:2,v<=2?color(57,50,43):color(255,255,255));
	}
	if(!has_move()) rect(0,0,240,2,color(240,60,60));
}

void retro_set_environment(env_cb cb) { environment=cb; if(cb){bool yes=true;cb(ENV_SUPPORT_NO_GAME,&yes);} }
void retro_set_video_refresh(video_cb cb) {video=cb;}
void retro_set_audio_sample(audio_cb cb) {(void)cb;}
void retro_set_audio_sample_batch(audio_batch_cb cb) {(void)cb;}
void retro_set_input_poll(poll_cb cb) {input_poll=cb;}
void retro_set_input_state(input_cb cb) {input_state=cb;}
void retro_set_controller_port_device(unsigned port,unsigned device) {(void)port;(void)device;}
unsigned retro_api_version(void) {return 1;}
void retro_init(void) {reset();}
void retro_deinit(void) {}
void retro_get_system_info(struct retro_system_info *i) {*i=(struct retro_system_info){"Sonix 2048","1.0.0","",false,false};}
void retro_get_system_av_info(struct retro_system_av_info *i) {*i=(struct retro_system_av_info){{W,H,W,H,1.0f},{FPS,44100.0}};}
bool retro_load_game(const struct retro_game_info *info) {
	if(info)return false; // native game, never load a ROM
	unsigned fmt=PIXEL_RGB565;
	if(!environment || !environment(ENV_PIXEL_FORMAT,&fmt)) return false;
	reset();return true;
}
bool retro_load_game_special(unsigned type,const struct retro_game_info *info,size_t count) {(void)type;(void)info;(void)count;return false;}
void retro_unload_game(void) {}
void retro_reset(void) {reset();}
void retro_run(void) {
	uint16_t keys=0;
	if(input_poll) input_poll();
	if(input_state) for(unsigned i=0;i<16;++i)
		if(input_state(0,DEVICE_JOYPAD,0,i)) keys|=(uint16_t)(1u<<i);
	uint16_t pressed=(uint16_t)(keys&~previous_keys);
	if(pressed&(1u<<3)) reset();
	else for(unsigned d=0;d<4;++d) if(pressed&(1u<<(d+4))) {
		if(slide(d)) {spawn();dirty=true;}
		break;
	}
	previous_keys=keys;
	if(dirty){draw();dirty=false;if(video)video(frame,W,H,W*sizeof(uint16_t));}
	else if(video)video(NULL,W,H,W*sizeof(uint16_t));
}
size_t retro_serialize_size(void) {return sizeof(game);}
bool retro_serialize(void *data,size_t size) {
	if(!data||size!=sizeof(game)) return false;
	memcpy(data,&game,size);return true;
}
bool retro_unserialize(const void *data,size_t size) {
	if(!data||size!=sizeof(game)) return false;
	const uint8_t *bytes=data;
	for(unsigned i=0;i<16;++i) if(bytes[offsetof(game_state_t,cells)+i]>30) return false;
	memcpy(&game,data,size); previous_keys=0;dirty=true;return true;
}
unsigned retro_get_region(void) {return 0;}
void *retro_get_memory_data(unsigned id) {(void)id;return NULL;}
size_t retro_get_memory_size(unsigned id) {(void)id;return 0;}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index,bool enabled,const char *code) {(void)index;(void)enabled;(void)code;}

#ifdef SONIX_CORE_TEST
#include <assert.h>
static bool test_env(unsigned cmd,void *data) {return cmd==ENV_SUPPORT_NO_GAME?*(bool*)data:cmd==ENV_PIXEL_FORMAT&&*(unsigned*)data==PIXEL_RGB565;}
static unsigned rendered;
static void test_video(const void *data,unsigned w,unsigned h,size_t pitch) {assert(w==W&&h==H&&pitch==W*2);if(data)++rendered;}
int main(void) {
	retro_set_environment(test_env);retro_set_video_refresh(test_video);
	retro_init();assert(retro_load_game(NULL));
	retro_run();retro_run();assert(rendered==1);
	game_state_t saved;assert(retro_serialize(&saved,sizeof(saved)));
	retro_reset();assert(retro_unserialize(&saved,sizeof(saved)));
	assert(!memcmp(&saved,&game,sizeof(game)));retro_run();assert(rendered==2);
	reset();
	unsigned n=0;
	for(unsigned i=0;i<16;++i) n+=!!game.cells[i];
	assert(n==2);
	memset(&game,0,sizeof(game));
	game.cells[0]=1;game.cells[1]=1;game.cells[2]=1;game.cells[3]=1;
	assert(slide(2));
	assert(game.cells[0]==2 && game.cells[1]==2 && !game.cells[2] && !game.cells[3] && game.score==8);
	assert(slide(2) && game.cells[0]==3 && !game.cells[1] && game.score==16);
	assert(!slide(2));
	draw(); assert(has_move());
	return 0;
}
#endif
