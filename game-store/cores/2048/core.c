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
#define FPS 30.0
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
static game_state_t game,undo_board;
static bool can_undo;
// libretro SAVE_RAM is stored separately from the version-specific save state.
static uint32_t best_score;
static uint32_t shown_best;
typedef struct { uint32_t magic; game_state_t board,undo; uint8_t can_undo; uint8_t reserved[3]; } snapshot_t;
#define SNAPSHOT_MAGIC 0x32303438u
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
	spawn(); spawn(); previous_keys=0;can_undo=false;memset(&undo_board,0,sizeof(undo_board));dirty=true;
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
static bool move(unsigned direction) {
	game_state_t old=game;
	if(!slide(direction)) return false;
	undo_board=old;can_undo=true;
	spawn();
	if(game.score>best_score)best_score=game.score;
	dirty=true;return true;
}
static bool undo_move(void) {
	if(!can_undo)return false;
	game=undo_board;can_undo=false;dirty=true;return true;
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
// Compact 3x5 uppercase glyphs. No font assets or dynamic allocation.
static uint16_t glyph(char c) {
	switch(c) {
	case 'A':return 0x2bed;case 'B':return 0x6bae;case 'C':return 0x3927;
	case 'D':return 0x6b6e;case 'E':return 0x79e7;case 'F':return 0x79e4;
	case 'G':return 0x396f;case 'H':return 0x5bed;case 'I':return 0x7497;
	case 'J':return 0x126e;case 'K':return 0x5bad;case 'L':return 0x4927;
	case 'M':return 0x5fed;case 'N':return 0x5fed;case 'O':return 0x7b6f;
	case 'P':return 0x7be4;case 'Q':return 0x7b7b;case 'R':return 0x6bad;
	case 'S':return 0x79cf;case 'T':return 0x7492;case 'U':return 0x5b6f;
	case 'V':return 0x5b6a;case 'W':return 0x5bfd;case 'X':return 0x5aad;
	case 'Y':return 0x5a92;case 'Z':return 0x73a7;case ':':return 0x0410;
	default:return 0;
	}
}
static void word(const char *s,int cx,int y,int scale,uint16_t c) {
	int x=cx-((int)strlen(s)*4*scale-scale)/2;
	for(;*s;++s,x+=4*scale) {
		uint16_t bits=(*s>='0'&&*s<='9')?digit[*s-'0']:glyph(*s);
		for(int r=0;r<5;++r)for(int col=0;col<3;++col)
			if(bits&(1u<<(14-r*3-col)))rect(x+col*scale,y+r*scale,scale,scale,c);
	}
}
static void roundrect(int x,int y,int w,int h,uint16_t c) {
	rect(x+2,y,w-4,h,c);rect(x,y+2,w,h-4,c);
	rect(x+1,y+1,w-2,h-2,c);
}
static void draw(void) {
	static const uint8_t palette[][3]={{78,72,68},{239,228,211},{237,222,189},{247,174,112},{246,143,86},{247,114,86},{240,84,55},{236,199,92},{236,187,71},{235,173,49},{225,154,23},{203,119,19}};
	rect(0,0,W,H,color(34,30,29));
	uint16_t ink=color(255,246,227),dim=color(186,172,158);
	roundrect(6,4,112,30,color(68,60,56));roundrect(122,4,112,30,color(68,60,56));
	word("SCORE",62,7,1,dim);word("BEST",178,7,1,dim);
	number(game.score,62,17,2,ink);number(best_score,178,17,2,ink);
	roundrect(6,39,228,184,color(100,87,78));
	for(unsigned i=0;i<16;++i) {
		uint8_t v=game.cells[i],k=v>11?11:v;
		int x=10+(int)(i%4)*55,y=43+(int)(i/4)*44;
		roundrect(x,y,51,40,color(palette[k][0],palette[k][1],palette[k][2]));
		if(v)number(1u<<v,x+25,y+14,v>13?1:2,v<=2?color(57,50,43):color(255,255,255));
	}
	word("START: NEW",62,229,1,dim);
	word("B: UNDO",178,229,1,can_undo?ink:dim);
	if(!has_move()){
		roundrect(23,87,194,78,color(42,35,32));
		word("GAME OVER",120,96,3,ink);
		word("START: NEW GAME",120,124,1,dim);
		word(can_undo?"B: UNDO LAST MOVE":"NO MOVES LEFT",120,142,1,ink);
	}
	shown_best=best_score;
}

void retro_set_environment(env_cb cb) { environment=cb; if(cb){bool yes=true;cb(ENV_SUPPORT_NO_GAME,&yes);} }
void retro_set_video_refresh(video_cb cb) {video=cb;}
void retro_set_audio_sample(audio_cb cb) {(void)cb;}
void retro_set_audio_sample_batch(audio_batch_cb cb) {(void)cb;}
void retro_set_input_poll(poll_cb cb) {input_poll=cb;}
void retro_set_input_state(input_cb cb) {input_state=cb;}
void retro_set_controller_port_device(unsigned port,unsigned device) {(void)port;(void)device;}
unsigned retro_api_version(void) {return 1;}
void retro_init(void) {best_score=shown_best=0;reset();}
void retro_deinit(void) {}
void retro_get_system_info(struct retro_system_info *i) {*i=(struct retro_system_info){"Sonix 2048","1.1.0","",false,false};}
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
	else if(pressed&1u) (void)undo_move();
	else for(unsigned d=0;d<4;++d) if(pressed&(1u<<(d+4))) {
		(void)move(d);
		break;
	}
	previous_keys=keys;
	if(game.score>best_score)best_score=game.score;
	if(best_score!=shown_best)dirty=true;
	if(dirty){draw();dirty=false;if(video)video(frame,W,H,W*sizeof(uint16_t));}
	else if(video)video(NULL,W,H,W*sizeof(uint16_t));
}
size_t retro_serialize_size(void) {return sizeof(snapshot_t);}
bool retro_serialize(void *data,size_t size) {
	if(!data||size!=sizeof(snapshot_t))return false;
	snapshot_t snap={0};
	snap.magic=SNAPSHOT_MAGIC;snap.board=game;snap.undo=undo_board;snap.can_undo=can_undo?1:0;
	memcpy(data,&snap,sizeof(snap));return true;
}
static bool valid_board(const game_state_t *b,bool allow_empty) {
	unsigned count=0;
	for(unsigned i=0;i<16;++i){if(b->cells[i]>30)return false;count+=!!b->cells[i];}
	return allow_empty||count>0;
}
bool retro_unserialize(const void *data,size_t size) {
	if(!data||size!=sizeof(snapshot_t))return false;
	snapshot_t snap;memcpy(&snap,data,sizeof(snap));
	if(snap.magic!=SNAPSHOT_MAGIC||snap.can_undo>1||!valid_board(&snap.board,false)||
		!valid_board(&snap.undo,!snap.can_undo))return false;
	game=snap.board;undo_board=snap.undo;can_undo=snap.can_undo!=0;
	if(game.score>best_score)best_score=game.score;
	previous_keys=0;dirty=true;return true;
}
unsigned retro_get_region(void) {return 0;}
void *retro_get_memory_data(unsigned id) {return id==0?&best_score:NULL;}
size_t retro_get_memory_size(unsigned id) {return id==0?sizeof(best_score):0;}
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
	snapshot_t saved;assert(retro_serialize(&saved,sizeof(saved)));
	retro_reset();assert(retro_unserialize(&saved,sizeof(saved)));
	assert(!memcmp(&saved.board,&game,sizeof(game)));retro_run();assert(rendered==2);
	assert(retro_get_memory_size(0)==sizeof(uint32_t));
	assert(retro_get_memory_data(0)==&best_score);
	*(uint32_t*)retro_get_memory_data(0)=100;
	retro_reset();assert(best_score==100);
	retro_run();assert(shown_best==100);
	memset(&game,0,sizeof(game));game.rng=123;game.cells[0]=1;game.cells[1]=1;
	game_state_t before=game;
	assert(move(2));assert(can_undo&&game.score==4&&best_score==100);
	assert(retro_serialize(&saved,sizeof(saved)));reset();
	assert(retro_unserialize(&saved,sizeof(saved))&&can_undo);
	assert(undo_move()&&!can_undo&&!memcmp(&game,&before,sizeof(game)));
	assert(!undo_move());
	saved.board.cells[0]=31;assert(!retro_unserialize(&saved,sizeof(saved)));
	for(unsigned i=0;i<16;++i)game.cells[i]=(uint8_t)(1+(i/4+i%4)%2);
	assert(!has_move());draw();
	game.cells[0]=0;assert(has_move());
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
