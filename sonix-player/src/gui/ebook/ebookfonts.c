#include "ebookfonts.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "src/gui/fonts/fonts.h"
#include "src/system/core/respath.h"
#include "src/system/core/config.h"
#include "src/system/ebook/ebookfontlist.h"

#define EBOOK_FONT_DIR SONIX_RESOURCE_DIR "/fonts/ebook"
#define JAPANESE_FONT_DIR SONIX_RESOURCE_DIR "/fonts"
typedef enum { FACE_REGULAR, FACE_ITALIC, FACE_BOLD, FACE_BOLD_ITALIC, FACE_COUNT } face_t;
static const char *const FACE_FILES[] = {"Bookerly-Regular.ttf","Bookerly-Italic.ttf","Bookerly-Bold.ttf","Bookerly-BoldItalic.ttf"};
static const char *const JAPANESE_FILES[] = {"japanese.otf",NULL,"japanese-bold.otf",NULL};
static const char *const *face_files = FACE_FILES;
static char font_dir[800], card_root[800], custom_id[EBOOKFONTLIST_ID];
static lv_font_t *faces[FACE_COUNT];
static bool tried[FACE_COUNT], have_regular, book_japanese;
static int current_size = 20;

static lv_font_t *create_font(const char *path, int size, bool japanese) {
 if(access(path,R_OK)) return NULL;
 lv_font_t *font=lv_freetype_font_create(path,LV_FREETYPE_FONT_RENDER_MODE_BITMAP,(uint32_t)size,LV_FREETYPE_FONT_STYLE_NORMAL);
 if(font) {
  font->fallback=&font_ui_20;
  if(japanese) {font->line_height=size*3/2; font->base_line=size*37/100;}
 }
 return font;
}

/* Independent candidate: selecting/previewing never invalidates live labels. */
static lv_font_t *candidate(const char *root,const char *id,int size,bool japanese,char *directory,size_t n) {
 char path[1024];
 if(directory&&n) directory[0]=0;
 if(id&&id[0]) {
  if(!ebookfontlist_resolve(root,id,0,path,sizeof(path))) return NULL;
  return create_font(path,size,false);
 }
 const char *file=japanese?JAPANESE_FILES[0]:FACE_FILES[0];
 char dir[800]; snprintf(dir,sizeof(dir),"%s",japanese?JAPANESE_FONT_DIR:EBOOK_FONT_DIR);
 snprintf(path,sizeof(path),"%s/%s",dir,file);
 lv_font_t *font=create_font(path,size,japanese);
 if(!font) {
  snprintf(dir,sizeof(dir),"%s/.local/fonts",root?root:"");
  snprintf(path,sizeof(path),"%s/%s",dir,file);
  font=create_font(path,size,japanese);
 }
 if(directory&&n) snprintf(directory,n,"%s",dir);
 return font;
}

static void close_faces(void) {
 for(int i=0;i<FACE_COUNT;i++) {
  if(faces[i]) lv_freetype_font_delete(faces[i]);
  faces[i]=NULL; tried[i]=false;
 }
 have_regular=false;
}

static lv_font_t *open_face(face_t which,int size) {
 char path[EBOOKFONTLIST_PATH];
 if(custom_id[0]) {
  if(!ebookfontlist_resolve(card_root,custom_id,(unsigned)which,path,sizeof(path))) return NULL;
 } else {
  if(!face_files[which]) return NULL;
  int n=snprintf(path,sizeof(path),"%s/%s",font_dir,face_files[which]);
  if(n<0||(size_t)n>=sizeof(path)) return NULL;
 }
 return create_font(path,size,!custom_id[0]&&book_japanese);
}

static void install(const char *root,const char *id,int size,bool japanese,const char *dir,lv_font_t *font) {
 /* Callers have detached all objects referencing the old faces. */
 close_faces();
 snprintf(card_root,sizeof(card_root),"%s",root?root:"");
 snprintf(custom_id,sizeof(custom_id),"%s",id?id:"");
 snprintf(font_dir,sizeof(font_dir),"%s",dir?dir:"");
 book_japanese=japanese; face_files=japanese?JAPANESE_FILES:FACE_FILES;
 current_size=size; faces[FACE_REGULAR]=font; have_regular=font!=NULL;
}

bool ebookfonts_open(const char *root,int size,bool japanese) {
 if(size<=0) size=20;
 char id[EBOOKFONTLIST_ID],dir[800];
 snprintf(id,sizeof(id),"%s",config_store_get(config_ebook_store(),"ebook","font_file",""));
 if(!ebookfontlist_valid_id(id)) id[0]=0;
 lv_font_t *font=candidate(root,id,size,japanese,dir,sizeof(dir));
 if(!font&&id[0]) {id[0]=0; font=candidate(root,id,size,japanese,dir,sizeof(dir));}
 install(root,id,size,japanese,dir,font);
 return have_regular;
}

lv_font_t *ebookfonts_preview(const char *root,const char *id,int size,bool japanese) {
 return candidate(root,id,size>0?size:20,japanese,NULL,0);
}

bool ebookfonts_choose(const char *root,const char *id,int size,bool japanese) {
 if(!id||(id[0]&&!ebookfontlist_valid_id(id))) return false;
 char saved[EBOOKFONTLIST_ID],dir[800];
 snprintf(saved,sizeof(saved),"%s",id);
 lv_font_t *font=candidate(root,saved,size>0?size:20,japanese,dir,sizeof(dir));
 if(saved[0]&&!font) return false; /* keep the active font and settings */
 install(root,saved,size>0?size:20,japanese,dir,font);
 config_store_set(config_ebook_store(),"ebook","font_file",saved);
 config_store_save(config_ebook_store());
 return true;
}

void ebookfonts_close(void) {close_faces(); font_dir[0]=card_root[0]=custom_id[0]=0;}
const char *ebookfonts_active_id(void) {return custom_id;}

bool ebookfonts_set_size(int size) {
 if(size<=0||size==current_size) return have_regular;
 bool wanted[FACE_COUNT];
 for(int i=0;i<FACE_COUNT;i++) wanted[i]=faces[i]!=NULL;
 char root[800],id[EBOOKFONTLIST_ID],dir[800];
 snprintf(root,sizeof(root),"%s",card_root); snprintf(id,sizeof(id),"%s",custom_id);
 bool japanese=book_japanese;
 lv_font_t *font=candidate(root,id,size,japanese,dir,sizeof(dir));
 if(!font&&id[0]) {id[0]=0; font=candidate(root,id,size,japanese,dir,sizeof(dir));}
 install(root,id,size,japanese,dir,font);
 for(int i=1;i<FACE_COUNT;i++) if(wanted[i]) faces[i]=open_face((face_t)i,size);
 return have_regular;
}

int ebookfonts_size(void) {return current_size;}
bool ebookfonts_present(void) {return have_regular;}
static lv_font_t *face_or_load(face_t which) {
 if(faces[which]) return faces[which];
 if(tried[which]) return NULL;
 tried[which]=true; faces[which]=open_face(which,current_size); return faces[which];
}
const lv_font_t *ebookfonts_face(bool bold,bool italic) {
 if(!have_regular) return &font_ui_20;
 if(bold&&italic) {
  lv_font_t *f=face_or_load(FACE_BOLD_ITALIC); if(f) return f;
  f=face_or_load(FACE_ITALIC); if(f) return f;
  f=face_or_load(FACE_BOLD); return f?f:faces[FACE_REGULAR];
 }
 if(italic) {lv_font_t *f=face_or_load(FACE_ITALIC); return f?f:faces[FACE_REGULAR];}
 if(bold) {lv_font_t *f=face_or_load(FACE_BOLD); return f?f:faces[FACE_REGULAR];}
 return faces[FACE_REGULAR];
}
