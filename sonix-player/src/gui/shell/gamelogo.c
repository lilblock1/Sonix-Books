#define _POSIX_C_SOURCE 200809L
#include "gamelogo.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/gui.h"
#include "src/system/device/system.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOGO_UI 72
typedef struct {
	lv_image_dsc_t image;
	uint8_t pixels[GAME_LOGO_BYTES];
} logo_data_t;
static void logo_destroy(lv_event_t *e) {free(lv_event_get_user_data(e));}
static bool installed_art(const char *id,uint8_t *out) {
	if(!gui_card_available()||!sdpackages_game_id(id))return false;
	char path[1200];
	if(!sdpackages_game_core(storage_sd_root(),id,path,sizeof(path)))return false;
	char *name=strrchr(path,'/');if(!name||(size_t)(name-path)+sizeof("/logo.rgb565")>sizeof(path))return false;
	memcpy(name,"/logo.rgb565",sizeof("/logo.rgb565"));
	int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return false;
	struct stat st;bool ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size==GAME_LOGO_BYTES;
	size_t pos=0;
	while(ok&&pos<GAME_LOGO_BYTES){
		ssize_t n=read(fd,out+pos,GAME_LOGO_BYTES-pos);
		if(n<=0)ok=false;else pos+=(size_t)n;
	}
	close(fd);return ok;
}
static void tile(lv_obj_t *parent,int x,int y,int w,int h,uint32_t rgb,const char *text) {
	lv_obj_t *o=lv_obj_create(parent);
	lv_obj_set_size(o,w,h);lv_obj_set_pos(o,x,y);lv_obj_set_scrollable(o,false);
	lv_obj_set_clickable(o,false);lv_obj_set_style_pad_all(o,0,0);
	lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,5,0);
	lv_obj_set_style_bg_color(o,lv_color_hex(rgb),0);
	lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
	lv_obj_t *l=lv_label_create(o);lv_label_set_text(l,text);
	lv_obj_set_style_text_font(l,&font_ui_18,0);
	lv_obj_set_style_text_color(l,lv_color_hex(0x332b24),0);lv_obj_center(l);
}
lv_obj_t *gamelogo_create(lv_obj_t *parent,const char *id,const char *title,game_store_kind_t kind,const uint8_t *art) {
	lv_obj_t *box=lv_obj_create(parent);
	lv_obj_set_size(box,LOGO_UI,LOGO_UI);lv_obj_set_scrollable(box,false);
	lv_obj_set_clickable(box,false);lv_obj_set_style_pad_all(box,0,0);
	lv_obj_set_style_border_width(box,0,0);lv_obj_set_style_radius(box,12,0);
	lv_obj_set_style_bg_color(box,lv_color_hex(0x34363d),0);
	lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);
	logo_data_t *data=calloc(1,sizeof(*data));
	if(data&&(art||installed_art(id,data->pixels))){
		if(art)memcpy(data->pixels,art,GAME_LOGO_BYTES);
		data->image.header.magic=LV_IMAGE_HEADER_MAGIC;
		data->image.header.cf=LV_COLOR_FORMAT_RGB565;
		data->image.header.w=GAME_LOGO_SIDE;data->image.header.h=GAME_LOGO_SIDE;
		data->image.header.stride=GAME_LOGO_SIDE*2;
		data->image.data_size=GAME_LOGO_BYTES;data->image.data=data->pixels;
		lv_obj_t *img=lv_image_create(box);lv_image_set_src(img,&data->image);
		lv_image_set_scale(img,384);lv_obj_center(img);
		lv_obj_add_event_cb(img,logo_destroy,LV_EVENT_DELETE,data);
		return box;
	}
	free(data);
	if(id&&!strcmp(id,"2048")){
		lv_obj_set_style_bg_color(box,lv_color_hex(0x786858),0);
		tile(box,5,5,29,29,0xeee4d4,"2");tile(box,38,5,29,29,0xeddebd,"4");
		tile(box,5,38,29,29,0xf7ae70,"8");tile(box,38,38,29,29,0xf68f56,"16");
	}else if(id&&(!strcmp(id,"gearboy")||!strcmp(id,"flappy-bird"))){
		const lv_image_dsc_t *src=!strcmp(id,"gearboy")?&icon_menu_gearboy:&icon_menu_flappy_bird;
		lv_obj_t *img=lv_image_create(box);lv_image_set_src(img,src);
		unsigned side=src->header.w>src->header.h?src->header.w:src->header.h;
		if(side>LOGO_UI)lv_image_set_scale(img,LOGO_UI*256/side);
		lv_obj_center(img);
	}else{
		char initials[3]={0};unsigned n=0;
		for(const char *p=title?title:"";*p&&n<2;++p)if((p==title||p[-1]==' '||p[-1]=='-')&&((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')))initials[n++]=*p;
		if(!n)memcpy(initials,"?",2);
		lv_obj_set_style_bg_color(box,lv_color_hex(kind==GAME_STORE_EMULATOR?0x395a85:0x536744),0);
		lv_obj_t *l=lv_label_create(box);lv_label_set_text(l,initials);
		lv_obj_set_style_text_font(l,&font_ui_32,0);lv_obj_set_style_text_color(l,lv_color_white(),0);lv_obj_center(l);
	}
	return box;
}
