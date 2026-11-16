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
static game_options_t options;
static bool fast_forward,gpsp_core,session_interpreter,skip_video,session_recovered;
static double native_fps,measured_fps,measured_video_fps;
static unsigned available_ram_kib,rendered_frames,request_slot;
static int state_result;
static int request;
static uint16_t keys,*latest;
static uint32_t serial;
static unsigned format;
static bool no_game,have_audio;
static int16_t sound[2048*2];
static size_t sound_used;
static game_resampler_t audio_resampler;
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
	if(ok&&options.fps!=fps){options.fps=(uint8_t)fps;fps_dirty=true;fps_save_error=false;}
	pthread_mutex_unlock(&lock);return ok;
}
void nativegame_get_fps(unsigned *limit,double *native,bool *save_error) {
	pthread_mutex_lock(&lock);if(limit)*limit=options.fps;if(native)*native=native_fps;if(save_error)*save_error=fps_save_error;pthread_mutex_unlock(&lock);
}
double nativegame_measured_fps(void) {pthread_mutex_lock(&lock);double fps=measured_fps;pthread_mutex_unlock(&lock);return fps;}
bool nativegame_get_options(game_options_t *out) {
	pthread_mutex_lock(&lock);bool ready=busy&&native_fps>0;if(out)*out=options;pthread_mutex_unlock(&lock);return ready;
}
bool nativegame_set_options(game_options_t value) {
	if(!game_options_valid(value))return false;
	pthread_mutex_lock(&lock);bool ok=busy&&!stop_requested&&native_fps>0;
	if(ok&&memcmp(&options,&value,sizeof(value))){options=value;fps_dirty=true;fps_save_error=false;}
	pthread_mutex_unlock(&lock);return ok;
}
void nativegame_set_fast_forward(bool enabled) {pthread_mutex_lock(&lock);fast_forward=busy&&!stop_requested&&enabled;pthread_mutex_unlock(&lock);}
bool nativegame_fast_forward(void) {pthread_mutex_lock(&lock);bool on=fast_forward;pthread_mutex_unlock(&lock);return on;}
void nativegame_diagnostics(double *fps,double *video_fps,unsigned *ram_kib) {
	pthread_mutex_lock(&lock);if(fps)*fps=measured_fps;if(video_fps)*video_fps=measured_video_fps;if(ram_kib)*ram_kib=available_ram_kib;pthread_mutex_unlock(&lock);
}
bool nativegame_recovered(void){pthread_mutex_lock(&lock);bool recovered=session_recovered;pthread_mutex_unlock(&lock);return recovered;}
int nativegame_state_result(void){pthread_mutex_lock(&lock);int result=state_result;pthread_mutex_unlock(&lock);return result;}
static void queue_state(int action){pthread_mutex_lock(&lock);if(busy&&!stop_requested&&native_fps>0){request=action;request_slot=options.slot;state_result=0;}pthread_mutex_unlock(&lock);}
void nativegame_save(void){queue_state(1);}
void nativegame_load(void){queue_state(2);}
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
		struct {const char *key,*value;} *v=data;v->value=NULL;
		if(gpsp_core&&session_interpreter&&v->key&&!strcmp(v->key,"gpsp_drc")){v->value="disabled";return true;}
		return false;
	}
	case 17: *(bool*)data=false;return true;
	case 18: no_game=*(bool*)data;return true;
	case 31: *(const char**)data=save_dir;return true;
	case 39: *(unsigned*)data=0;return true; // preferred software context
	
	default:return false;
	}
}
static void video(const void *p,unsigned w,unsigned h,size_t pitch) {
	if(skip_video || !p || p==(void*)-1 || !w || !h || w>1024 || h>1024 || pitch<w*(format==1?4u:2u))return;
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
			// GBA 240x160 and other exact 2x RGB565 sources: duplicate a
			// pixel with a single 32-bit store, no map lookup or conversion.
			if(format==2&&dw==2*w){
				for(unsigned x=0;x<w;++x){uint16_t q;memcpy(&q,src+x*2,2);uint32_t pair=(uint32_t)q|((uint32_t)q<<16);memcpy(dst+x*2,&pair,4);}
				continue;
			}
			for(unsigned x=0;x<dw;++x) {
				unsigned i=xmap[x];
				if(format==1){uint32_t q;memcpy(&q,src+i*4,4);dst[x]=(uint16_t)(((q>>8)&0xf800)|((q>>5)&0x7e0)|((q>>3)&31));}
				else {uint16_t q;memcpy(&q,src+i*2,2);dst[x]=format==2?q:(uint16_t)(((q&0x7c00)<<1)|((q&0x3e0)<<1)|((q&0x200)>>4)|(q&31));}
			}
		}
		++serial;++rendered_frames;
	}
	pthread_mutex_unlock(&lock);
}
static void flush_audio(void) {if(have_audio && sound_used)audio_external_write(sound,(int)sound_used);sound_used=0;}
static void emit_pcm(int16_t l,int16_t r) {
	sound[2*sound_used]=l;sound[2*sound_used+1]=r;
	if(++sound_used==2048)flush_audio();
}
static void sample(int16_t l,int16_t r) {
	if(have_audio)game_resampler_push(&audio_resampler,l,r,emit_pcm);
}
static size_t batch(const int16_t *p,size_t n) {
	if(!have_audio||!p)return n;
	for(size_t i=0;i<n;++i){
		if((i&2047u)==0&&stopping())break;
		game_resampler_push(&audio_resampler,p[2*i],p[2*i+1],emit_pcm);
	}
	return n;
}
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
static bool state(core_t *c,int saves,const char *name,bool load) {
	size_t n=c->retro_serialize_size();if(saves<0||!n||n>STATE_MAX)return false;
	void *buf=malloc(n);if(!buf)return false;
	bool ok=load?(file_read(saves,name,buf,n)&&c->retro_unserialize(buf,n)):(c->retro_serialize(buf,n)&&file_write(saves,name,buf,n));
	free(buf);return ok;
}
// Preserve a suspect automatic state before writing a fresh one. Keep up to
// nine independent recovery copies, never overwrite a prior backup.
static bool quarantine_state(int saves,const char *name,const char *digest){
	struct stat st;
	if(fstatat(saves,name,&st,AT_SYMLINK_NOFOLLOW))return errno==ENOENT;
	if(!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>STATE_MAX)return false;
	for(unsigned i=1;i<=9;++i){
		char backup[80];snprintf(backup,sizeof(backup),"%s.recover%c",digest,(int)('0'+i));
		if(fstatat(saves,backup,&st,AT_SYMLINK_NOFOLLOW)==0)continue;
		if(errno!=ENOENT)return false;
		return renameat(saves,name,saves,backup)==0;
	}
	return false;
}
static unsigned ram_available(void){
	FILE *f=fopen("/proc/meminfo","r");if(!f)return 0;
	char line[128];unsigned n=0;
	while(fgets(line,sizeof(line),f))if(sscanf(line,"MemAvailable: %u kB",&n)==1)break;
	fclose(f);return n;
}
static void sram(core_t *c,int saves,const char *name,bool load) {
	size_t n=c->retro_get_memory_size(0);void *p=c->retro_get_memory_data(0);
	if(saves<0||!n||n>STATE_MAX||!p)return;
	if(load) {void *b=malloc(n);if(b){if(file_read(saves,name,b,n))memcpy(p,b,n);free(b);}}
	else file_write(saves,name,p,n);
}
static void *run(void *arg) {
	session_t *s=arg;core_t c={0};void *lib=NULL,*rom_data=NULL;bool init=false,loaded=false,marked=false,recovered=false,recovery_saved=false;int saves=-1;
	char card[64],core[1100],rom[1100],state_name[80],save_name[80],fps_name[80],options_name[80],running_name[80],digest[65],state_digest[65],generation[SDPACKAGES_VERSION],version[SDPACKAGES_VERSION];
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
	snprintf(options_name,sizeof(options_name),"%s.options",digest);
	game_options_t stored=game_options_default();stored.fps=stored_fps;
	game_options_t disk;
	if(file_read(saves,options_name,&disk,sizeof(disk))&&game_options_valid(disk))stored=disk;
		snprintf(running_name,sizeof(running_name),"%s.running",digest);
	uint8_t marker=0;
	recovered=file_read(saves,running_name,&marker,1)&&marker==1;
	// Write before dlopen/init. A clean close removes it. A power loss is
	// indistinguishable from a crash; never change the user's stored options.
	marker=1;
	marked=recovered||file_write(saves,running_name,&marker,1);
	if(marked)(void)fsync(saves);
	gpsp_core=!strcmp(s->id,"gpsp");session_interpreter=stored.jit==GAME_JIT_INTERPRETER||recovered;skip_video=false;rendered_frames=0;
	pthread_mutex_lock(&lock);session_recovered=recovered;pthread_mutex_unlock(&lock);
	// Save states are engine-version specific; battery saves survive updates.
	sha256_init(&hash);sha256_update(&hash,s->rom,strlen(s->rom)+1);sha256_update(&hash,version,strlen(version));sha256_final_hex(&hash,digest);
	snprintf(state_name,sizeof(state_name),"%s.state",digest);
	memcpy(state_digest,digest,sizeof(state_digest));
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
	sram(&c,saves,save_name,true);if(!recovered)state(&c,saves,state_name,true);
	pthread_mutex_lock(&lock);native_fps=av.timing.fps;options=stored;pthread_mutex_unlock(&lock);
	int audio_rate=0;unsigned audio_buffer=0;
	unsigned perf_frames=0,perf_rendered=0;struct timespec perf_start={0};
	bool overloaded=false,previous_skip=false;
	for(;;){
		pthread_mutex_lock(&lock);
		bool stop=stop_requested,on_hold=paused,dirty=fps_dirty;
		game_options_t opt=options;bool ff=fast_forward;
		unsigned slot=request_slot;
		int req=request;request=0;fps_dirty=false;
		pthread_mutex_unlock(&lock);
		if(dirty){
			bool ok=file_write(saves,options_name,&opt,sizeof(opt));
			pthread_mutex_lock(&lock);fps_save_error=!ok;pthread_mutex_unlock(&lock);
		}
		if(stop)break;
		if(req){
			char slot_name[80];
			// Manual slots are separate from the legacy automatic resume file.
			snprintf(slot_name,sizeof(slot_name),"%s.slot%c.state",state_digest,(int)('1'+(slot%4)));
			bool ok=state(&c,saves,slot_name,req==2);
			pthread_mutex_lock(&lock);if(!request)state_result=ok?req:-1;pthread_mutex_unlock(&lock);
			if(req==2){sound_used=0;game_resampler_reset(&audio_resampler,(unsigned)audio_rate,GAME_PCM_RATE);}
		}
		if(on_hold){
			if(have_audio){audio_external_end();have_audio=false;}
			sound_used=0;audio_rate=0;perf_frames=0;overloaded=previous_skip=false;
			struct timespec delay={0,20000000};nanosleep(&delay,NULL);continue;
		}
		int rate=0;
		if(!ff&&av.timing.sample_rate>=8000.0&&av.timing.sample_rate<=96000.0)
			rate=game_audio_rate(av.timing.fps,av.timing.sample_rate,opt.fps);
		if(rate!=audio_rate||audio_buffer!=opt.buffer_ms){
			if(have_audio)audio_external_end();
			have_audio=false;audio_rate=rate;audio_buffer=opt.buffer_ms;
			sound_used=0;game_resampler_reset(&audio_resampler,(unsigned)rate,GAME_PCM_RATE);
			if(rate>0&&rate<=96000)have_audio=audio_external_begin_latency(GAME_PCM_RATE,2,16,(int)audio_buffer);
		}
		int64_t period=game_period_ns(av.timing.fps,opt.fps);
		// Explicit session-only 1.5x mode; ordinary limits remain respected.
		if(ff)period=(period*2+2)/3;
		skip_video=game_should_skip(ff?GAME_SKIP_HALF:opt.skip,previous_skip,overloaded);
		previous_skip=skip_video;
		struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
		if(!perf_frames){perf_start=a;perf_rendered=rendered_frames;}
		c.retro_run();
		clock_gettime(CLOCK_MONOTONIC,&b);
		int64_t work=(int64_t)(b.tv_sec-a.tv_sec)*1000000000+b.tv_nsec-a.tv_nsec;
		// Keep overload latched across a skipped frame, until a rendered frame is cheap.
		if(!skip_video)overloaded=work>period*9/10;
		flush_audio();++perf_frames;
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
		clock_gettime(CLOCK_MONOTONIC,&b);
		int64_t span=(int64_t)(b.tv_sec-perf_start.tv_sec)*1000000000+b.tv_nsec-perf_start.tv_nsec;
		if(span>=1000000000){unsigned ram=ram_available();pthread_mutex_lock(&lock);measured_fps=perf_frames*1000000000.0/span;measured_video_fps=(rendered_frames-perf_rendered)*1000000000.0/span;available_ram_kib=ram;pthread_mutex_unlock(&lock);perf_frames=0;}
	}
	// Preserve the prior auto-state before writing new progress after recovery.
	if(!recovered)state(&c,saves,state_name,false);
	else if(quarantine_state(saves,state_name,state_digest))
		recovery_saved=state(&c,saves,state_name,false);
	sram(&c,saves,save_name,false);
done:
	if(have_audio)audio_external_end();
	have_audio=false;
	if(loaded)c.retro_unload_game();
	if(init)c.retro_deinit();
	if(lib)dlclose(lib);
	if(marked&&saves>=0&&(!recovered||recovery_saved)){
		(void)unlinkat(saves,running_name,0);(void)fsync(saves);
	}
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
	paused=fps_dirty=fps_save_error=fast_forward=session_recovered=false;options=game_options_default();native_fps=measured_fps=measured_video_fps=0;available_ram_kib=0;request_slot=0;state_result=0;
	pthread_mutex_unlock(&lock);
	audio_stop();
	pthread_t thread;
	if(pthread_create(&thread,NULL,run,s)){
		close(s->card);free(s);pthread_mutex_lock(&lock);free(latest);latest=NULL;busy=false;failed=true;pthread_mutex_unlock(&lock);return false;
	}
	pthread_detach(thread);return true;
}
