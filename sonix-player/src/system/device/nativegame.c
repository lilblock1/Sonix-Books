#define _GNU_SOURCE
#include "nativegame.h"
#include "nativegame_timing.h"
#include "sdpackages.h"
#include "system.h"
#include "ota.h"
#include "src/system/audio/audio.h"
#include "src/system/core/sha256.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#define ROM_MAX (8u*1024u*1024u)
#define CORE_MAX (12u*1024u*1024u)
#define STATE_MAX (2u*1024u*1024u)
#define PIXELS (NATIVEGAME_W*NATIVEGAME_H_PIX)
typedef bool (*env_t)(unsigned,void*);
typedef void (*video_t)(const void*,unsigned,unsigned,size_t);
typedef void (*audio_t)(int16_t,int16_t);
typedef size_t (*batch_t)(const int16_t*,size_t);
typedef void (*poll_t)(void);
typedef int16_t (*input_t)(unsigned,unsigned,unsigned,unsigned);
typedef struct { const char *path;const void *data;size_t size;const char *meta; } game_info_t;
typedef struct { const char *library_name,*library_version,*valid_extensions;bool need_fullpath,block_extract; } system_info_t;
typedef struct { struct {unsigned base_width,base_height,max_width,max_height;float aspect_ratio;} geometry;
	struct {double fps,sample_rate;} timing; } av_info_t;
typedef struct {
	unsigned (*retro_api_version)(void);
	void (*retro_init)(void),(*retro_deinit)(void),(*retro_run)(void),(*retro_unload_game)(void);
	void (*retro_set_environment)(env_t),(*retro_set_video_refresh)(video_t),(*retro_set_audio_sample)(audio_t),
		(*retro_set_audio_sample_batch)(batch_t),(*retro_set_input_poll)(poll_t),(*retro_set_input_state)(input_t);
	void (*retro_get_system_info)(system_info_t*),(*retro_get_system_av_info)(av_info_t*);
	bool (*retro_load_game)(const game_info_t*);
	size_t (*retro_serialize_size)(void);
	bool (*retro_serialize)(void*,size_t),(*retro_unserialize)(const void*,size_t);
	void *(*retro_get_memory_data)(unsigned);
	size_t (*retro_get_memory_size)(unsigned);
} core_t;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static bool busy,stop_requested,failed;
static bool paused,fps_dirty,fps_save_error;
static unsigned fps_limit;
static double native_fps;
static int request;
static uint16_t keys,*latest;
static uint32_t serial;
static unsigned format;
static bool no_game,have_audio;
static int16_t sound[2048*2];
static size_t sound_used;
static char core_dir[1100],save_dir[1100];
typedef struct {char id[SD_GAME_ID_MAX],rom[256]; int card;} session_t;
static bool stopping(void) { pthread_mutex_lock(&lock);bool s=stop_requested;pthread_mutex_unlock(&lock);return s; }
bool nativegame_busy(void) {pthread_mutex_lock(&lock);bool b=busy;pthread_mutex_unlock(&lock);return b;}
bool nativegame_failed(void) {pthread_mutex_lock(&lock);bool b=failed;pthread_mutex_unlock(&lock);return b;}
void nativegame_stop(void) {pthread_mutex_lock(&lock);stop_requested=true;keys=0;pthread_mutex_unlock(&lock);}
void nativegame_keys(uint16_t k) {pthread_mutex_lock(&lock);keys=paused?0:k;pthread_mutex_unlock(&lock);}
void nativegame_pause(bool on) {pthread_mutex_lock(&lock);paused=on;keys=0;pthread_mutex_unlock(&lock);}
bool nativegame_set_fps(unsigned fps) {
	if(!game_fps_valid(fps))return false;
	pthread_mutex_lock(&lock);bool ok=busy&&!stop_requested&&native_fps>0;
	if(ok&&fps_limit!=fps){fps_limit=fps;fps_dirty=true;fps_save_error=false;}
	pthread_mutex_unlock(&lock);return ok;
}
void nativegame_get_fps(unsigned *limit,double *native,bool *save_error) {
	pthread_mutex_lock(&lock);if(limit)*limit=fps_limit;if(native)*native=native_fps;if(save_error)*save_error=fps_save_error;pthread_mutex_unlock(&lock);
}
void nativegame_save(void) {pthread_mutex_lock(&lock);request=1;pthread_mutex_unlock(&lock);}
void nativegame_load(void) {pthread_mutex_lock(&lock);request=2;pthread_mutex_unlock(&lock);}
bool nativegame_present(uint16_t *pixels,uint32_t *seen) {
	pthread_mutex_lock(&lock);
	bool changed=latest && serial!=*seen;
	if(changed){memcpy(pixels,latest,PIXELS*2);*seen=serial;}
	pthread_mutex_unlock(&lock);return changed;
}
static bool environment(unsigned cmd,void *data) {
	if (!data) return false;
	switch(cmd) {
	case 3: *(bool*)data=true; return true; // can duplicate frame
	case 9: *(const char**)data=core_dir;return true; // system/BIOS files
	case 10: if(*(unsigned*)data>2)return false;format=*(unsigned*)data;return true;
	case 15: { // GET_VARIABLE: default core options only
		struct {const char *key,*value;} *v=data;v->value=NULL;return false;
	}
	case 17: *(bool*)data=false;return true;
	case 18: no_game=*(bool*)data;return true;
	case 31: *(const char**)data=save_dir;return true;
	case 39: *(unsigned*)data=0;return true; // preferred software context
	
	default:return false;
	}
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch) {
	if(!p || p==(void*)-1 || !w || !h || w>1024 || h>1024 || pitch<w*(format==1?4u:2u))return;
	unsigned dw=NATIVEGAME_W,dh=(unsigned)((uint64_t)h*dw/w);
	if(dh>NATIVEGAME_H_PIX){dh=NATIVEGAME_H_PIX;dw=(unsigned)((uint64_t)w*dh/h);}
	if(!dw||!dh)return;
	unsigned x0=(NATIVEGAME_W-dw)/2,y0=(NATIVEGAME_H_PIX-dh)/2;
	// Only the game worker uses these maps. Recompute on geometry changes,
	// not for every destination pixel; copy repeated source rows directly.
	static uint16_t xmap[NATIVEGAME_W],ymap[NATIVEGAME_H_PIX];
	static unsigned old_w,old_h,old_dw,old_dh;
	bool changed=old_w!=w||old_h!=h||old_dw!=dw||old_dh!=dh;
	if(changed){
		for(unsigned x=0;x<dw;++x)xmap[x]=(uint16_t)(x*w/dw);
		for(unsigned y=0;y<dh;++y)ymap[y]=(uint16_t)(y*h/dh);
		old_w=w;old_h=h;old_dw=dw;old_dh=dh;
	}
	pthread_mutex_lock(&lock);
	if(latest){
		// A new session starts with a zeroed framebuffer; bars change only
		// when the geometry changes.
		if(changed)memset(latest,0,PIXELS*2);
		for(unsigned y=0;y<dh;++y) {
			const unsigned char *src=(const unsigned char*)p+(size_t)ymap[y]*pitch;
			uint16_t *dst=latest+(y+y0)*NATIVEGAME_W+x0;
			if(y&&ymap[y]==ymap[y-1]){memcpy(dst,dst-NATIVEGAME_W,dw*2u);continue;}
			for(unsigned x=0;x<dw;++x) {
				unsigned i=xmap[x];
				if(format==1){uint32_t q;memcpy(&q,src+i*4,4);dst[x]=(uint16_t)(((q>>8)&0xf800)|((q>>5)&0x7e0)|((q>>3)&31));}
				else {uint16_t q;memcpy(&q,src+i*2,2);dst[x]=format==2?q:(uint16_t)(((q&0x7c00)<<1)|((q&0x3e0)<<1)|((q&0x200)>>4)|(q&31));}
			}
		}
		++serial;
	}
	pthread_mutex_unlock(&lock);
}
static void flush_audio(void) {if(have_audio && sound_used)audio_external_write(sound,(int)sound_used);sound_used=0;}
static size_t batch(const int16_t *p,size_t n) {
	if(!have_audio || !p)return n;
	flush_audio();
	for(size_t i=0;i<n && !stopping();) {size_t q=n-i;if(q>2048)q=2048;audio_external_write(p+2*i,(int)q);i+=q;}
	return n;
}
static void sample(int16_t l,int16_t r) {if(!have_audio)return;sound[2*sound_used]=l;sound[2*sound_used+1]=r;if(++sound_used==2048)flush_audio();}
static void input_poll(void){}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id) {
	(void)index;if(port||device!=1||id>=16)return 0;
	pthread_mutex_lock(&lock);int16_t value=(keys&(1u<<id))!=0;pthread_mutex_unlock(&lock);return value;
}
static bool regular(const char *p,size_t max,size_t *length) {
	struct stat st;if(lstat(p,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>max)return false;
	if(length)*length=(size_t)st.st_size;
	return true;
}
static bool filename(const char *name) {
	if(!name||!*name||strlen(name)>=256||name[0]=='.')return false;
	for(const unsigned char *p=(const unsigned char*)name;*p;++p)if(*p<32||*p=='/'||*p=='\\'||*p==127)return false;
	return true;
}
// A verified package can still be corrupted on a removable SD. Recheck the
// installed member digest, then use an anonymous-on-close tmpfs copy if the
// existing mount forbids executable mappings. Never remount or touch flash.
static bool copy_checked(int input,int output,const char *expected) {
	sha256_ctx hash;sha256_init(&hash);
	unsigned char bytes[16384];size_t total=0;
	for(;;) {
		ssize_t got=read(input,bytes,sizeof(bytes));
		if(got<0||stopping())return false;
		if(!got)break;
		total+=(size_t)got;if(total>CORE_MAX)return false;
		sha256_update(&hash,bytes,(size_t)got);
		if(output>=0)for(ssize_t at=0;at<got;) {
			ssize_t sent=write(output,bytes+at,(size_t)(got-at));if(sent<=0)return false;at+=sent;
		}
	}
	char digest[65];sha256_final_hex(&hash,digest);
	return total>0&&!strcmp(digest,expected);
}
static void *open_core(const char *path) {
	char marker[1200],digest[67];
	int n=snprintf(marker,sizeof(marker),"%s/.core-sha256",core_dir);
	if(n<=0||(size_t)n>=sizeof(marker)||!regular(marker,65,NULL))return NULL;
	int hashfd=open(marker,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(hashfd<0)return NULL;
	ssize_t length=read(hashfd,digest,sizeof(digest));close(hashfd);
	if(length!=65||digest[64]!='\n')return NULL;
	digest[64]=0;
	if(!regular(path,CORE_MAX,NULL))return NULL;
	int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return NULL;
	if(!copy_checked(fd,-1,digest)){close(fd);return NULL;}
	void *lib=dlopen(path,RTLD_NOW|RTLD_LOCAL);
	if(lib){close(fd);return lib;}
	struct statfs fs;
	if(statfs("/tmp",&fs)||fs.f_type!=0x01021994){close(fd);return NULL;}
	char temp[]="/tmp/sonix-core-XXXXXX";
	int tmp=mkstemp(temp);
	if(tmp<0){close(fd);return NULL;}
	bool ok=!fstatfs(tmp,&fs)&&fs.f_type==0x01021994&&lseek(fd,0,SEEK_SET)==0&&copy_checked(fd,tmp,digest);
	close(fd);if(close(tmp))ok=false;
	if(ok)lib=dlopen(temp,RTLD_NOW|RTLD_LOCAL);
	unlink(temp);
	return lib;
}
static bool get_core(void *lib,core_t *c) {
#define LOAD(name) do { *(void **)(&c->name)=dlsym(lib,#name);if(!c->name)return false; } while(0)
	LOAD(retro_api_version);LOAD(retro_init);LOAD(retro_deinit);LOAD(retro_run);LOAD(retro_unload_game);
	LOAD(retro_set_environment);LOAD(retro_set_video_refresh);LOAD(retro_set_audio_sample);
	LOAD(retro_set_audio_sample_batch);LOAD(retro_set_input_poll);LOAD(retro_set_input_state);
	LOAD(retro_get_system_info);LOAD(retro_get_system_av_info);LOAD(retro_load_game);
	LOAD(retro_serialize_size);LOAD(retro_serialize);LOAD(retro_unserialize);
	LOAD(retro_get_memory_data);LOAD(retro_get_memory_size);
#undef LOAD
	return c->retro_api_version()==1;
}
static int folder_at(int parent,const char *name) {
	(void)mkdirat(parent,name,0755);
	return openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
}
static int saves_open(int card,const char *id) {
	int root=folder_at(card,".sonix");if(root<0)return -1;
	int saves=folder_at(root,"game-saves");close(root);if(saves<0)return -1;
	int game=folder_at(saves,id);close(saves);return game;
}
static bool file_read(int dir,const char *name,void *data,size_t length) {
	int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return false;
	struct stat st;bool ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&(uint64_t)st.st_size==length;
	size_t offset=0;while(ok&&offset<length){ssize_t n=read(fd,(char*)data+offset,length-offset);if(n<=0)ok=false;else offset+=(size_t)n;}
	close(fd);return ok;
}
static bool file_write(int dir,const char *name,const void *data,size_t length) {
	char tmp[100];int n=snprintf(tmp,sizeof(tmp),".%s.part",name);if(n<=0||(size_t)n>=sizeof(tmp))return false;
	// Only one core session runs; refuse any existing symlink or non-regular scratch.
	int fd=openat(dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return false;
	bool ok=true;size_t offset=0;
	while(offset<length){ssize_t q=write(fd,(const char*)data+offset,length-offset);if(q<=0){ok=false;break;}offset+=(size_t)q;}
	if(ok&&fsync(fd))ok=false;
	if(close(fd))ok=false;
	if(ok&&renameat(dir,tmp,dir,name))ok=false;
	if(!ok)unlinkat(dir,tmp,0);
	return ok;
}
static void state(core_t *c,int saves,const char *name,bool load) {
	size_t n=c->retro_serialize_size();if(saves<0||!n||n>STATE_MAX)return;
	void *buf=malloc(n);if(!buf)return;
	if(load){if(file_read(saves,name,buf,n))c->retro_unserialize(buf,n);}
	else if(c->retro_serialize(buf,n))file_write(saves,name,buf,n);
	free(buf);
}
static void sram(core_t *c,int saves,const char *name,bool load) {
	size_t n=c->retro_get_memory_size(0);void *p=c->retro_get_memory_data(0);
	if(saves<0||!n||n>STATE_MAX||!p)return;
	if(load) {void *b=malloc(n);if(b){if(file_read(saves,name,b,n))memcpy(p,b,n);free(b);}}
	else file_write(saves,name,p,n);
}
static void *run(void *arg) {
	session_t *s=arg;core_t c={0};void *lib=NULL,*rom_data=NULL;bool init=false,loaded=false;int saves=-1;
	char card[64],core[1100],rom[1100],state_name[80],save_name[80],fps_name[80],digest[65],generation[SDPACKAGES_VERSION],version[SDPACKAGES_VERSION];
	snprintf(card,sizeof(card),"/proc/self/fd/%d/.",s->card);
	if(!sdpackages_game_core(card,s->id,core,sizeof(core))||!sdpackages_game_generation(card,s->id,generation,version))goto done;
	memcpy(core_dir,core,strlen(core)+1);*strrchr(core_dir,'/')=0;
	saves=saves_open(s->card,s->id);if(saves<0)goto done;
	snprintf(save_dir,sizeof(save_dir),"/proc/self/fd/%d/.",saves);
	sha256_ctx hash;sha256_init(&hash);sha256_update(&hash,s->rom,strlen(s->rom));sha256_final_hex(&hash,digest);
	snprintf(save_name,sizeof(save_name),"%s.sav",digest);
	snprintf(fps_name,sizeof(fps_name),"%s.fps",digest);
	uint8_t stored_fps=0;
	if(!file_read(saves,fps_name,&stored_fps,sizeof(stored_fps))||!game_fps_valid(stored_fps))stored_fps=0;
	// Save states are engine-version specific; battery saves survive updates.
	sha256_init(&hash);sha256_update(&hash,s->rom,strlen(s->rom)+1);sha256_update(&hash,version,strlen(version));sha256_final_hex(&hash,digest);
	snprintf(state_name,sizeof(state_name),"%s.state",digest);
	lib=open_core(core);if(!lib||!get_core(lib,&c))goto done;
	format=0;no_game=false;have_audio=false;sound_used=0;
	c.retro_set_environment(environment);c.retro_set_video_refresh(video);c.retro_set_audio_sample(sample);
	c.retro_set_audio_sample_batch(batch);c.retro_set_input_poll(input_poll);c.retro_set_input_state(input);
	c.retro_init();init=true;
	system_info_t info={0};c.retro_get_system_info(&info);
	game_info_t game={0};
	if(s->rom[0]){
		int n=snprintf(rom,sizeof(rom),"%s/Games/%s/ROMs/%s",card,s->id,s->rom);
		if(n<=0||(size_t)n>=sizeof(rom)||!regular(rom,info.need_fullpath?128u*1024u*1024u:ROM_MAX,&game.size))goto done;
		game.path=rom;
		if(!info.need_fullpath){
			FILE *f=fopen(rom,"rb");rom_data=malloc(game.size);
			bool ok=f&&rom_data&&fread(rom_data,1,game.size,f)==game.size;
			if(f)fclose(f);
			if(!ok)goto done;
			game.data=rom_data;
		}
	}else if(!no_game)goto done;
	if(!c.retro_load_game(s->rom[0]?&game:NULL))goto done;
	loaded=true;av_info_t av={0};c.retro_get_system_av_info(&av);
	if(!(av.timing.fps>=10.0&&av.timing.fps<=120.0)||av.geometry.max_width>1024||av.geometry.max_height>1024)goto done;
	sram(&c,saves,save_name,true);state(&c,saves,state_name,true);
	pthread_mutex_lock(&lock);native_fps=av.timing.fps;fps_limit=stored_fps;pthread_mutex_unlock(&lock);
	int audio_rate=0;
	for(;;){
		pthread_mutex_lock(&lock);
		bool stop=stop_requested,on_hold=paused,dirty=fps_dirty;
		unsigned limit=fps_limit;
		int req=request;request=0;fps_dirty=false;
		pthread_mutex_unlock(&lock);
		if(dirty){
			uint8_t val=(uint8_t)limit;
			bool ok=file_write(saves,fps_name,&val,sizeof(val));
			pthread_mutex_lock(&lock);fps_save_error=!ok;pthread_mutex_unlock(&lock);
		}
		if(stop)break;
		if(req)state(&c,saves,state_name,req==2);
		if(on_hold){
			if(have_audio){audio_external_end();have_audio=false;}
			audio_rate=0;
			struct timespec delay={0,20000000};nanosleep(&delay,NULL);continue;
		}
		int rate=0;
		if(av.timing.sample_rate>=8000.0&&av.timing.sample_rate<=96000.0)
			rate=game_audio_rate(av.timing.fps,av.timing.sample_rate,limit);
		if(rate!=audio_rate){
			if(have_audio)audio_external_end();
			have_audio=false;audio_rate=rate;
			if(rate>=8000&&rate<=96000)have_audio=audio_external_begin_latency(rate,2,16,64);
		}
		int64_t period=game_period_ns(av.timing.fps,limit);
		struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
		c.retro_run();flush_audio();
		// Start-to-start cap, including PCM blocking; never catch up by
		// running bursts after a slow frame. Retry interrupted sleeps.
		for(;;){
			clock_gettime(CLOCK_MONOTONIC,&b);
			int64_t elapsed=(int64_t)(b.tv_sec-a.tv_sec)*1000000000+b.tv_nsec-a.tv_nsec;
			if(elapsed>=period||stopping())break;
			int64_t ns=period-elapsed;
			struct timespec delay={(time_t)(ns/1000000000),(long)(ns%1000000000)};
			if(nanosleep(&delay,NULL)==0||errno!=EINTR)break;
		}
	}
	state(&c,saves,state_name,false);sram(&c,saves,save_name,false);
done:
	if(have_audio)audio_external_end();
	have_audio=false;
	if(loaded)c.retro_unload_game();
	if(init)c.retro_deinit();
	if(lib)dlclose(lib);
	free(rom_data);if(saves>=0)close(saves);close(s->card);free(s);
	pthread_mutex_lock(&lock);failed=!stop_requested;free(latest);latest=NULL;busy=false;pthread_mutex_unlock(&lock);
	return NULL;
}
bool nativegame_start(const char *id,const char *rom) {
	if(!sdpackages_game_id(id)||(rom&&*rom&&!filename(rom))||ota_busy()||!storage_card_attached())return false;
	pthread_mutex_lock(&lock);
	if(busy){pthread_mutex_unlock(&lock);return false;}
	session_t *s=calloc(1,sizeof(*s));
	if(!s){pthread_mutex_unlock(&lock);return false;}
	s->card=open(storage_sd_root(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
	struct stat card,parent;
	if(s->card<0||fstat(s->card,&card)||fstatat(s->card,"..",&parent,0)
#ifndef HOST_BUILD
		||card.st_dev==parent.st_dev
#endif
	){if(s->card>=0)close(s->card);free(s);pthread_mutex_unlock(&lock);return false;}
	latest=calloc(PIXELS,sizeof(*latest));
	if(!latest){close(s->card);free(s);pthread_mutex_unlock(&lock);return false;}
	snprintf(s->id,sizeof(s->id),"%s",id);if(rom)snprintf(s->rom,sizeof(s->rom),"%s",rom);
	busy=true;failed=stop_requested=false;request=0;keys=0;serial=0;
	paused=fps_dirty=fps_save_error=false;fps_limit=0;native_fps=0;
	pthread_mutex_unlock(&lock);
	audio_stop();
	pthread_t thread;
	if(pthread_create(&thread,NULL,run,s)){
		close(s->card);free(s);pthread_mutex_lock(&lock);free(latest);latest=NULL;busy=false;failed=true;pthread_mutex_unlock(&lock);return false;
	}
	pthread_detach(thread);return true;
}
