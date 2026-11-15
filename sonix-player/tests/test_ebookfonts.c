#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#define MKDIR(p) mkdir(p,0700)
#else
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#endif
#include "src/gui/ebook/ebookfonts.h"
#include "src/system/ebook/ebookfontlist.h"
#include "src/system/core/config.h"
lv_font_t font_ui_20;
static unsigned created,deleted,live,peak;
lv_font_t *lv_freetype_font_create(const char *path,lv_freetype_font_render_mode_t mode,uint32_t size,lv_freetype_font_style_t style) {
 assert(mode==LV_FREETYPE_FONT_RENDER_MODE_BITMAP&&style==LV_FREETYPE_FONT_STYLE_NORMAL);
 FILE *f=fopen(path,"rb"); if(!f)return NULL; fclose(f);
 if(strstr(path,"Reject"))return NULL;
 lv_font_t *font=calloc(1,sizeof(*font));assert(font);
 font->line_height=(int)size;font->base_line=(int)size/4;created++;live++;if(live>peak)peak=live;return font;
}
void lv_freetype_font_delete(lv_font_t *font) {assert(font&&font!=&font_ui_20&&live);deleted++;live--;free(font);}
static void dir(const char *path) {assert(MKDIR(path)==0);}
static void fake(const char *path) {
 unsigned char bytes[32]={0,1,0,0,0,1,0,0,0,0,0,0,'h','e','a','d',0,0,0,0,0,0,0,28,0,0,0,4,0,0,0,0};
 if(strstr(path,".otf"))memcpy(bytes,"OTTO",4);
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));fclose(f);
}
static void flush(void) {
 config_store_t *store=config_ebook_store();
#ifdef _WIN32
 if(!config_store_save(store)){remove(config_store_path(store));assert(config_store_save(store));}
#else
 assert(config_store_save(store));
#endif
 char saved[512];snprintf(saved,sizeof(saved),"%s",config_store_path(store));assert(config_store_init(store,saved));
}
int main(int argc,char **argv) {
 assert(argc==2);const char *root=argv[1];char cfg[512];snprintf(cfg,sizeof(cfg),"%s/ebook.ini",root);
 config_store_t *store=config_ebook_store();(void)config_store_init(store,cfg);
 config_store_set(store,"ebook","pos_existing","3:14:8|p1:37:0");
 dir("Ebook");dir("Ebook/Fonts");dir("Ebook/Fonts/Alpha");
 fake("Ebook/Fonts/Alpha/Regular.ttf");fake("Ebook/Fonts/Alpha/Italic.ttf");fake("Ebook/Fonts/Alpha/Bold.ttf");
 fake("Ebook/Fonts/Zed.otf");fake("Ebook/Fonts/Reject.ttf");fake("Ebook/Fonts/.hidden.ttf");
 FILE *f=fopen("Ebook/Fonts/Broken.ttf","w");assert(f);fputs("bad",f);fclose(f);
 ebookfontlist_t list;ebookfontlist_scan(root,&list);
 assert(list.count==3&&!list.unavailable&&!list.truncated&&list.skipped);
 assert(!strcmp(list.entries[0].name,"Alpha")&&!strcmp(list.entries[2].name,"Zed"));
 /* Include the separator, filename and terminator in the family ID limit. */
 char family[EBOOKFONTLIST_ID],boundary[256],boundary_id[EBOOKFONTLIST_ID];
 const size_t max_family=EBOOKFONTLIST_ID-sizeof("/Regular.ttf");
 for(unsigned otf=0;otf<2;otf++){
  memset(family,otf?'Y':'X',max_family);family[max_family]=0;
  snprintf(boundary,sizeof(boundary),"Ebook/Fonts/%s",family);dir(boundary);
  snprintf(boundary,sizeof(boundary),"Ebook/Fonts/%s/Regular.%s",family,otf?"otf":"ttf");fake(boundary);
  memcpy(boundary_id,family,max_family);
  memcpy(boundary_id+max_family,otf?"/Regular.otf":"/Regular.ttf",sizeof("/Regular.ttf"));
  assert(strlen(boundary_id)==EBOOKFONTLIST_ID-1&&ebookfontlist_valid_id(boundary_id));
  ebookfontlist_scan(root,&list);assert(list.count==4+(int)otf);
  int found=0;
  for(int i=0;i<list.count;i++)if(!strcmp(list.entries[i].id,boundary_id))found=1;
  assert(found);
 }
 memset(family,'W',max_family+1);family[max_family+1]=0;
 snprintf(boundary,sizeof(boundary),"Ebook/Fonts/%s",family);dir(boundary);
 snprintf(boundary,sizeof(boundary),"Ebook/Fonts/%s/Regular.otf",family);fake(boundary);
 ebookfontlist_scan(root,&list);assert(list.count==5&&list.skipped>=2);
 char file[800];assert(ebookfontlist_resolve(root,"Alpha/Regular.ttf",0,file,sizeof(file)));
 assert(ebookfontlist_resolve(root,"Alpha/Regular.ttf",1,file,sizeof(file)));
 assert(!ebookfontlist_resolve(root,"Alpha/Regular.ttf",3,file,sizeof(file)));
 assert(!ebookfontlist_resolve(root,"Zed.otf",1,file,sizeof(file)));
 const char *bad[]={"../bad.ttf","/bad.ttf","Alpha/../Regular.ttf","Alpha//Regular.ttf","Alpha\\Regular.ttf","C:bad.ttf",".hidden.ttf","Alpha/Regular.ttf/extra"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!ebookfontlist_valid_id(bad[i]));
 assert(!ebookfontlist_resolve(root,"../bad.ttf",0,file,sizeof(file))&&!file[0]);
 assert(!ebookfontlist_resolve(root,"Alpha/Regular.ttf",0,file,4)&&!file[0]);
 dir("usr");dir("usr/resource");dir("usr/resource/sonix");dir("usr/resource/sonix/fonts");dir("usr/resource/sonix/fonts/ebook");
 fake("usr/resource/sonix/fonts/ebook/Bookerly-Regular.ttf");fake("usr/resource/sonix/fonts/ebook/Bookerly-Italic.ttf");
 fake("usr/resource/sonix/fonts/ebook/Bookerly-Bold.ttf");fake("usr/resource/sonix/fonts/ebook/Bookerly-BoldItalic.ttf");
 fake("usr/resource/sonix/fonts/japanese.otf");fake("usr/resource/sonix/fonts/japanese-bold.otf");
 assert(ebookfonts_open(root,20,false)&&!ebookfonts_active_id()[0]);
 const lv_font_t *default_face=ebookfonts_face(false,false);
 lv_font_t *preview=ebookfonts_preview(root,"Alpha/Regular.ttf",20,false);assert(preview);
 assert(ebookfonts_face(false,false)==default_face);lv_freetype_font_delete(preview);
 assert(ebookfonts_choose(root,"Alpha/Regular.ttf",22,false));flush();
 assert(!strcmp(ebookfonts_active_id(),"Alpha/Regular.ttf"));
 const lv_font_t *regular=ebookfonts_face(false,false),*italic=ebookfonts_face(false,true);
 assert(regular&&regular->fallback==&font_ui_20&&italic!=regular);
 assert(ebookfonts_face(true,true)==italic);assert(ebookfonts_face(true,false)!=regular);
 unsigned stable=created;assert(ebookfonts_face(true,true)==italic&&created==stable);
 assert(!ebookfonts_choose(root,"Reject.ttf",22,false));
 assert(ebookfonts_face(false,false)==regular&&!strcmp(config_store_get(store,"ebook","font_file",""),"Alpha/Regular.ttf"));
 for(int i=0;i<40;i++){preview=ebookfonts_preview(root,"Zed.otf",20,false);assert(preview);lv_freetype_font_delete(preview);}
 assert(ebookfonts_set_size(30)&&ebookfonts_size()==30&&ebookfonts_face(false,false)->line_height==30);
 assert(remove("Ebook/Fonts/Alpha/Regular.ttf")==0);
 assert(ebookfonts_open(root,20,false)&&!ebookfonts_active_id()[0]);
 assert(!strcmp(config_store_get(store,"ebook","font_file",""),"Alpha/Regular.ttf"));
 fake("Ebook/Fonts/Alpha/Regular.ttf");assert(ebookfonts_open(root,20,false)&&ebookfonts_active_id()[0]);
 assert(ebookfonts_choose(root,"",24,true));flush();
 assert(ebookfonts_face(false,false)->line_height==36&&ebookfonts_face(false,false)->base_line==8);
 assert(!strcmp(config_store_get(store,"ebook","pos_existing",""),"3:14:8|p1:37:0"));
 config_store_set(store,"ebook","font_file","../outside.ttf");assert(ebookfonts_open(root,20,false)&&!ebookfonts_active_id()[0]);
 ebookfonts_close();assert(!live&&created==deleted&&peak<=6);
 for(int i=34;i>=0;i--){char path[128];snprintf(path,sizeof(path),"Ebook/Fonts/Font%02d.ttf",i);fake(path);}
 ebookfontlist_scan(root,&list);assert(list.count==32&&list.truncated&&!strcmp(list.entries[0].name,"Alpha"));
 assert(!strcmp(list.entries[31].name,"Font30"));
#ifndef _WIN32
 assert(symlink("/tmp","Ebook/Fonts/Escape")==0);
 assert(!ebookfontlist_resolve(root,"Escape/Regular.ttf",0,file,sizeof(file)));
#endif
 puts("ebook fonts: catalog bounds, path checks, previews, persistence, styles, default fallback and face lifetimes passed");
 return 0;
}
