#define _POSIX_C_SOURCE 200809L
#include "gamespage.h"
#include "src/gui/flappybird/flappybird.h"
#include "src/gui/gearboy/gearboypage.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/topbar.h"
#include "src/system/device/nativegame.h"
#include "src/system/gearboy/gbinput.h"
#include <stdatomic.h>
#include "src/system/device/sdpackages.h"
#include "src/system/device/system.h"
#include "src/system/device/power.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_GAMES 48
#define MAX_ROMS 96
lv_obj_t *gamespage_screen;
static lv_obj_t *games_list,*rom_screen,*rom_list,*play_screen,*picture,*status;
static lv_timer_t *timer;
static gui_config_t *config;
static char games[MAX_GAMES][SD_GAME_ID_MAX],roms[MAX_ROMS][256],chosen[SD_GAME_ID_MAX];
static unsigned game_count,rom_count;
static uint16_t *pixels,buttons;
static uint32_t seen;
static lv_image_dsc_t image;
static bool held,raw_claimed;
static uint16_t raw_previous;
static atomic_uint session_generation;
static gbinput_zone_t zones[16];
static unsigned zone_count;

static lv_obj_t *make_list(lv_obj_t *screen,const char *title) {
	lv_obj_add_style(screen,&theme_style_screen,0);
	settingsrow_title(screen,config,title);
	int top=settingsrow_content_top(config);
	lv_obj_t *list=lv_obj_create(screen);
	lv_obj_set_size(list,lv_pct(100),config->screen_height-top);
	lv_obj_align(list,LV_ALIGN_TOP_LEFT,0,top);
	lv_obj_set_style_bg_opa(list,0,0);
	lv_obj_set_style_border_width(list,0,0);
	lv_obj_set_style_pad_hor(list,config->padding,0);
	lv_obj_set_style_pad_row(list,8,0);
	lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scroll_dir(list,LV_DIR_VER);
	switcher_attach_back_gesture(screen);
	switcher_attach_back_gesture(list);
	return list;
}
static void info(lv_obj_t *list,const char *message) {
	lv_obj_t *label=lv_label_create(list);
	lv_obj_set_width(label,lv_pct(100));
	lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
	lv_label_set_text(label,message);
	lv_obj_add_style(label,&theme_style_text_dim,0);
	lv_obj_set_style_text_font(label,&font_ui_20,0);
}
static bool available(void) {
	if(nativegame_busy()){gui_notify_popup("Previous game is closing.");return false;}
	if(switcher_back_drag_active())return false;
	return true;
}
#if GB_CORE
static void gearboy_clicked(lv_event_t *e) {(void)e;if(available())switch_screen(gearboypage_screen);}
#endif
static void flappy_clicked(lv_event_t *e) {(void)e;if(available())flappybird_open();}
static void refresh_frame(lv_timer_t *t) {
	(void)t;
	if(nativegame_failed()){lv_label_set_text(status,"Could not start game. Check core, ROM and SD compatibility.");return;}
	if(pixels&&nativegame_present(pixels,&seen)) {
		lv_obj_invalidate(picture);
		lv_label_set_text(status,"");
	}
}
static void leave_play(lv_event_t *e) {
	(void)e;
	nativegame_stop();
	switch_screen_return_to(rom_screen);
}
static void raw_exit_ui(void *arg) {
	if ((unsigned)(uintptr_t)arg == atomic_load(&session_generation) && lv_screen_active()==play_screen) leave_play(NULL);
}
static void raw_exit(void) { (void)gui_post(raw_exit_ui,(void*)(uintptr_t)atomic_load(&session_generation)); }
static void raw_keys(uint16_t mask) {
	uint16_t pressed=mask & (uint16_t)~raw_previous;
	raw_previous=mask;
	if(pressed&(1u<<12))nativegame_save();
	if(pressed&(1u<<13))nativegame_load();
	nativegame_keys(mask&0x0fff);
}
static void unloaded(lv_event_t *e) {
	(void)e;
	lv_timer_pause(timer);
	atomic_fetch_add(&session_generation,1);
	if(raw_claimed){gbinput_stop();raw_claimed=false;}
	nativegame_stop();
	nativegame_keys(0);buttons=0;
	lv_image_set_src(picture,NULL);image.data=NULL;free(pixels);pixels=NULL;
	if(held){held=false;power_hold_screen_on(false);}
	back_btn_force_hidden(false);
	topbar_set_hidden(false);
}
static void game_key(lv_event_t *e) {
	lv_event_code_t code=lv_event_get_code(e);
	uint16_t key=(uint16_t)(uintptr_t)lv_event_get_user_data(e);
	if(code==LV_EVENT_PRESSED)buttons|=key;
	else if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)buttons&=(uint16_t)~key;
	else return;
	nativegame_keys(buttons);
}
static void save_clicked(lv_event_t *e) {(void)e;nativegame_save();}
static void load_clicked(lv_event_t *e) {(void)e;nativegame_load();}
static lv_obj_t *pad(const char *text,int x,int y,int w,int h,lv_event_cb_t cb,uintptr_t data) {
	lv_obj_t *b=lv_btn_create(play_screen);
	lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
	lv_obj_add_style(b,&theme_style_card,0);
	lv_obj_add_style(b,&theme_style_card_pressed,LV_STATE_PRESSED);
	lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_add_style(l,&theme_style_text,0);
	lv_obj_set_style_text_font(l,&font_ui_20,0);lv_obj_center(l);
	lv_obj_add_event_cb(b,cb,cb==game_key?LV_EVENT_ALL:LV_EVENT_CLICKED,(void*)data);
	if(zone_count<16) {
		uint16_t key=cb==game_key?(uint16_t)data:cb==save_clicked?1u<<12:cb==load_clicked?1u<<13:GBINPUT_KEY_MENU;
		zones[zone_count++]=(gbinput_zone_t){x,y,w,h,key};
	}
	return b;
}
static void launch(const char *rom) {
	if(!available())return;
	if(!gui_card_available()){gui_notify_no_card();return;}
	pixels=calloc((size_t)NATIVEGAME_W*NATIVEGAME_H_PIX,sizeof(*pixels));
	if(!pixels){gui_notify_popup("Not enough memory to start game.");return;}
	if(!nativegame_start(chosen,rom)){free(pixels);pixels=NULL;gui_notify_popup("Cannot start game.");return;}
	seen=0;buttons=0;
	memset(&image,0,sizeof(image));
	image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_RGB565;
	image.header.w=NATIVEGAME_W;image.header.h=NATIVEGAME_H_PIX;image.header.stride=NATIVEGAME_W*2;
	image.data_size=NATIVEGAME_W*NATIVEGAME_H_PIX*2;image.data=(const uint8_t*)pixels;
	lv_image_set_src(picture,&image);
	lv_label_set_text(status,"Starting native game...");
	switch_screen(play_screen);
	topbar_set_hidden(true);back_btn_force_hidden(true);
	held=true;power_hold_screen_on(true);
	raw_previous=0;atomic_fetch_add(&session_generation,1);
	gbinput_set_glass(config->screen_width,config->screen_height);
	raw_claimed=gbinput_start_for(zones,(int)zone_count,raw_exit,raw_keys);
	lv_timer_resume(timer);lv_timer_ready(timer);
}
static void launch_no_rom(lv_event_t *e) {(void)e;launch(NULL);}
static void launch_rom(lv_event_t *e) {
	unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(e);
	if(i<rom_count)launch(roms[i]);
}
static void show_roms(lv_event_t *e) {
	unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(e);
	if(i>=game_count||!available())return;
	if(!gui_card_available()){gui_notify_no_card();return;}
	snprintf(chosen,sizeof(chosen),"%s",games[i]);
	lv_obj_clean(rom_list);rom_count=0;
	settingsrow_action(rom_list,"Launch without ROM",launch_no_rom,NULL);
	char path[1024];
	int n=snprintf(path,sizeof(path),"%s/Games/%s/ROMs",storage_sd_root(),chosen);
	if(n>0&&(size_t)n<sizeof(path)){
		DIR *dir=opendir(path);
		if(dir){
			struct dirent *entry;
			while(rom_count<MAX_ROMS&&(entry=readdir(dir))){
				if(entry->d_name[0]=='.'||strlen(entry->d_name)>=sizeof(roms[0]))continue;
				char full[1400];int m=snprintf(full,sizeof(full),"%s/%s",path,entry->d_name);
				struct stat st;
				if(m<=0||(size_t)m>=sizeof(full)||lstat(full,&st)||!S_ISREG(st.st_mode))continue;
				memcpy(roms[rom_count],entry->d_name,strlen(entry->d_name)+1);
				settingsrow_action(rom_list,roms[rom_count],launch_rom,(void*)(uintptr_t)rom_count);
				++rom_count;
			}
			closedir(dir);
		}
	}
	char message[160];snprintf(message,sizeof(message),"ROMs: Games/%s/ROMs on microSD. Native games can launch without a ROM.",chosen);
	info(rom_list,message);
	switch_screen(rom_screen);
}
static void refresh_library(lv_event_t *e) {
	(void)e;lv_obj_clean(games_list);game_count=0;
#if GB_CORE
	settingsrow_action(games_list,"Game Boy / Game Boy Color (Gearboy)",gearboy_clicked,NULL);
#endif
	settingsrow_action(games_list,"flappy_bird",flappy_clicked,NULL);
	if(!gui_card_available()){info(games_list,"Insert microSD for downloaded games.");return;}
	const char *root=storage_sd_root();char path[1024];
	int n=snprintf(path,sizeof(path),"%s/.sonix",root);
	if(n<=0||(size_t)n>=sizeof(path))return;
	DIR *dir=opendir(path);
	if(dir){
		struct dirent *entry;
		while(game_count<MAX_GAMES&&(entry=readdir(dir))){
			if(strncmp(entry->d_name,"game-",5))continue;
			const char *id=entry->d_name+5;char core[1100],generation[SDPACKAGES_VERSION],version[SDPACKAGES_VERSION],label[112];
			if(!sdpackages_game_id(id)||!sdpackages_game_core(root,id,core,sizeof(core))||
				!sdpackages_game_generation(root,id,generation,version))continue;
			snprintf(games[game_count],sizeof(games[0]),"%s",id);
			snprintf(label,sizeof(label),"%s  %s",id,version);
			settingsrow_action(games_list,label,show_roms,(void*)(uintptr_t)game_count);++game_count;
		}
		closedir(dir);
	}
	if(!game_count)info(games_list,"Download more games and emulators from More > Game Store.");
}
void gamespage_init(gui_config_t *cfg) {
	config=cfg;games_list=make_list(gamespage_screen,"Game Launcher");
	lv_obj_add_event_cb(gamespage_screen,refresh_library,LV_EVENT_SCREEN_LOADED,NULL);
	rom_screen=lv_obj_create(NULL);rom_list=make_list(rom_screen,"Launch game");
	play_screen=lv_obj_create(NULL);
	lv_obj_add_style(play_screen,&theme_style_screen,0);
	lv_obj_set_scrollable(play_screen,false);
	lv_obj_set_style_bg_color(play_screen,lv_color_black(),0);
	picture=lv_image_create(play_screen);lv_obj_align(picture,LV_ALIGN_TOP_MID,0,22);
	status=lv_label_create(play_screen);
	lv_obj_set_width(status,cfg->screen_width-20);
	lv_obj_set_style_text_color(status,lv_color_white(),0);
	lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
	lv_label_set_long_mode(status,LV_LABEL_LONG_WRAP);
	lv_obj_align(status,LV_ALIGN_TOP_MID,0,120);
	// HiBy R1 portrait layout. Existing raw multitouch provider is shared;
	// the simulator retains the conventional single-pointer buttons.
	pad("L",12,394,98,54,game_key,1u<<10);pad("R",370,394,98,54,game_key,1u<<11);
	pad("UP",85,455,72,66,game_key,1u<<4);
	pad("LEFT",9,525,72,66,game_key,1u<<6);pad("RIGHT",161,525,72,66,game_key,1u<<7);
	pad("DOWN",85,595,72,66,game_key,1u<<5);
	pad("Y",298,455,67,66,game_key,1u<<1);pad("X",375,455,67,66,game_key,1u<<9);
	pad("B",298,525,67,66,game_key,1u<<0);pad("A",375,525,67,66,game_key,1u<<8);
	pad("SELECT",249,607,106,54,game_key,1u<<2);pad("START",364,607,106,54,game_key,1u<<3);
	pad("Save",12,696,142,60,save_clicked,0);pad("Load",169,696,142,60,load_clicked,0);pad("Exit",326,696,142,60,leave_play,0);
	lv_obj_add_event_cb(play_screen,unloaded,LV_EVENT_SCREEN_UNLOADED,NULL);
	timer=lv_timer_create(refresh_frame,16,NULL);lv_timer_pause(timer);
}
