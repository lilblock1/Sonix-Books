#include "game_store.h"
#include "ota_policy.h"
#include "src/system/core/json.h"
#include <stdio.h>
#include <string.h>

#define PREFIX "sonix-game-v1-"
#define SUFFIX ".spk"

typedef struct {const char *id;game_store_profile_t profile;} profile_entry_t;
static const profile_entry_t profiles[]={
	{"2048",{"Sonix 2048","Slide and merge tiles. Play offline; no ROM required.",GAME_STORE_GAME,true}},
	{"quicknes",{"QuickNES","Nintendo Entertainment System emulator. Requires your own NES ROMs.",GAME_STORE_EMULATOR,false}},
	{"nes",{"QuickNES","Nintendo Entertainment System emulator. Requires your own NES ROMs.",GAME_STORE_EMULATOR,false}},
	{"gpsp",{"gpSP","Game Boy Advance emulator. Requires your own GBA ROMs and any required BIOS.",GAME_STORE_EMULATOR,false}},
	{"gba",{"gpSP","Game Boy Advance emulator. Requires your own GBA ROMs and any required BIOS.",GAME_STORE_EMULATOR,false}},
	{"snes9x",{"Snes9x","Super Nintendo emulator. Requires your own SNES ROMs.",GAME_STORE_EMULATOR,false}},
	{"picodrive",{"PicoDrive","Sega Genesis / Mega Drive emulator. Requires your own ROMs.",GAME_STORE_EMULATOR,false}},
	{"doom",{"Doom","Classic first-person shooter engine. Requires compatible game data.",GAME_STORE_GAME,false}},
	{"freedoom",{"Freedoom","Open-source Doom-compatible game data and engine.",GAME_STORE_GAME,false}},
	{"snake",{"Snake","Classic arcade puzzle.",GAME_STORE_GAME,true}},
	{"minesweeper",{"Minesweeper","Logic puzzle. No ROM required.",GAME_STORE_GAME,true}},
	{"solitaire",{"Solitaire","Classic single-player card game.",GAME_STORE_GAME,true}}
};
const game_store_profile_t *game_store_profile(const char *id) {
	if(id)for(size_t i=0;i<sizeof(profiles)/sizeof(*profiles);++i)
		if(!strcmp(id,profiles[i].id))return &profiles[i].profile;
	return NULL;
}
static void display_metadata(game_store_item_t *item,const json_doc_t *doc,int asset) {
	const game_store_profile_t *p=game_store_profile(item->id);
	if(p) {
		snprintf(item->title,sizeof(item->title),"%s",p->title);
		snprintf(item->description,sizeof(item->description),"%s",p->description);
		item->kind=p->kind;
	}else{
		snprintf(item->title,sizeof(item->title),"%s",item->id);
		for(unsigned i=0;item->title[i];++i) {
			if(item->title[i]=='-')item->title[i]=' ';
			else if((!i||item->title[i-1]==' ')&&item->title[i]>='a'&&item->title[i]<='z')item->title[i]-='a'-'A';
		}
		snprintf(item->description,sizeof(item->description),"Downloadable native package. Check its requirements before playing.");
	}
	// Optional GitHub asset label: sonix-ui1|game/emulator|title|description.
	// Display text only: never influences the URL, ABI, file paths or control mapping.
	char label[256];
	int tok=json_get(doc,asset,"label");
	if(tok<0||doc->toks[tok].type!=JSON_STRING||
		doc->toks[tok].end-doc->toks[tok].start>=(int)sizeof(label)||
		!json_str(doc,tok,label,sizeof(label)))return;
	for(const unsigned char *q=(const unsigned char*)label;*q;++q)if(*q<32||*q>126)return;
	const char *prefix="sonix-ui1|";if(strncmp(label,prefix,strlen(prefix)))return;
	char *kind=label+strlen(prefix),*title=strchr(kind,'|');
	if(!title)return;
	*title++=0;
	char *description=strchr(title,'|');
	if(!description)return;
	*description++=0;
	if(!*title||!*description||strchr(description,'|')||strlen(title)>=sizeof(item->title)||strlen(description)>=sizeof(item->description))return;
	game_store_kind_t k=!strcmp(kind,"game")?GAME_STORE_GAME:!strcmp(kind,"emulator")?GAME_STORE_EMULATOR:GAME_STORE_OTHER;
	if(k==GAME_STORE_OTHER)return;
	item->kind=k;memcpy(item->title,title,strlen(title)+1);memcpy(item->description,description,strlen(description)+1);
}

static bool version_ok(const char *s, long out[4]) {
	char canonical[SDPACKAGES_VERSION];
	if (!ota_policy_version(s, out)) return false;
	int n = snprintf(canonical, sizeof(canonical), "v%ld.%ld.%ld", out[0], out[1], out[2]);
	return n > 0 && (size_t)n < sizeof(canonical) && !strcmp(s, canonical);
}
int game_store_version_cmp(const char *a, const char *b) {
	long x[4], y[4];
	if (!version_ok(a, x)) return -1;
	if (!version_ok(b, y)) return 1;
	return ota_policy_version_cmp(x,y);
}
static bool asset_id(const char *name, game_store_item_t *item) {
	size_t n = strlen(name), start = sizeof(PREFIX)-1, end = sizeof(SUFFIX)-1;
	if (n <= start + end || strncmp(name,PREFIX,start) || strcmp(name+n-end,SUFFIX)) return false;
	const char *version = NULL;
	for (const char *p = name+start; p < name+n-end; ++p)
		if (p[0]=='-' && p[1]=='v') version=p+1;
	if (!version || version==name+start) return false;
	size_t id_len=(size_t)(version-name-start-1), ver_len=(size_t)(name+n-end-version);
	if (!id_len || id_len>=sizeof(item->id) || !ver_len || ver_len>=sizeof(item->version)) return false;
	memcpy(item->id,name+start,id_len); item->id[id_len]=0;
	memcpy(item->version,version,ver_len); item->version[ver_len]=0;
	long parts[4];
	return sdpackages_game_id(item->id) && version_ok(item->version,parts);
}
bool game_store_parse(const char *body, game_store_item_t *items, size_t capacity, size_t *count) {
	if (!body || !items || !count || capacity > GAME_STORE_MAX || strlen(body)>=GAME_STORE_JSON_MAX) return false;
	*count=0;
	json_doc_t doc;
	if (!json_parse(body,&doc)) return false;
	int root=json_root(&doc), assets=json_get(&doc,root,"assets");
	char tag[64];
	bool ok=root>=0 && doc.toks[root].type==JSON_OBJECT && assets>=0 &&
		doc.toks[assets].type==JSON_ARRAY && json_obj_str(&doc,root,"tag_name",tag,sizeof(tag)) &&
		!strcmp(tag,GAME_STORE_TAG) && !json_obj_bool(&doc,root,"draft",true);
	for (int i=0; ok && i<json_len(&doc,assets); ++i) {
		int a=json_at(&doc,assets,i);
		char name[128], state[24], digest[80], expected[256];
		game_store_item_t item={0};
		if (!json_obj_str(&doc,a,"name",name,sizeof(name)) || !asset_id(name,&item)) continue;
		item.size=json_obj_long(&doc,a,"size",0);
		int n=snprintf(expected,sizeof(expected),"https://github.com/%s/releases/download/%s/%s",GAME_STORE_REPO,GAME_STORE_TAG,name);
		if (n<0 || (size_t)n>=sizeof(expected) || item.size<=0 || (unsigned long)item.size>SDPACKAGES_MAX_BYTES ||
			!json_obj_str(&doc,a,"state",state,sizeof(state)) || strcmp(state,"uploaded") ||
			!json_obj_str(&doc,a,"browser_download_url",item.url,sizeof(item.url)) || strcmp(expected,item.url) ||
			!json_obj_str(&doc,a,"digest",digest,sizeof(digest)) || !ota_policy_digest(digest)) continue;
		display_metadata(&item,&doc,a);
		memcpy(item.sha256,digest+7,65);
		for (int j=0; item.sha256[j]; ++j) if (item.sha256[j]>='A' && item.sha256[j]<='F') item.sha256[j]+='a'-'A';
		size_t k=0;
		while(k<*count && strcmp(items[k].id,item.id)) ++k;
		if(k<*count && game_store_version_cmp(item.version,items[k].version)<=0) continue;
		if(k==*count) { if(*count>=capacity) {ok=false;break;} ++*count; }
		items[k]=item;
	}
	// Artwork is a separate fixed-size, immutable release asset associated
	// with the selected package version. It cannot change package identity.
	for(size_t k=0;ok&&k<*count;++k) {
		char wanted[128],expected[256];
		int n=snprintf(wanted,sizeof(wanted),"sonix-logo-v1-%s-%s.rgb565",items[k].id,items[k].version);
		if(n<=0||(size_t)n>=sizeof(wanted))continue;
		n=snprintf(expected,sizeof(expected),"https://github.com/%s/releases/download/%s/%s",GAME_STORE_REPO,GAME_STORE_TAG,wanted);
		if(n<=0||(size_t)n>=sizeof(expected))continue;
		for(int i=0;i<json_len(&doc,assets);++i) {
			int a=json_at(&doc,assets,i);
			char name[128],state[24],url[256],digest[80];
			if(!json_obj_str(&doc,a,"name",name,sizeof(name))||strcmp(name,wanted)||
				json_obj_long(&doc,a,"size",0)!=GAME_LOGO_BYTES||
				!json_obj_str(&doc,a,"state",state,sizeof(state))||strcmp(state,"uploaded")||
				!json_obj_str(&doc,a,"browser_download_url",url,sizeof(url))||strcmp(url,expected)||
				!json_obj_str(&doc,a,"digest",digest,sizeof(digest))||!ota_policy_digest(digest))continue;
			memcpy(items[k].logo_url,expected,strlen(expected)+1);
			memcpy(items[k].logo_sha256,digest+7,65);
			for(unsigned j=0;j<64;++j)if(items[k].logo_sha256[j]>='A'&&items[k].logo_sha256[j]<='F')items[k].logo_sha256[j]+='a'-'A';
			break;
		}
	}
	json_free(&doc);
	if(!ok) *count=0;
	return ok;
}


void store_ui_default(store_ui_t *out) {
	memset(out,0,sizeof(*out));
	strcpy(out->version,"built-in");
	strcpy(out->heading,"Game Store");
	strcpy(out->subtitle,"Browse games and emulators. Tap for details.");
	out->layout=STORE_UI_LIST;out->palette=STORE_UI_SYSTEM;
	out->radius=12;out->padding=12;out->gap=8;out->logo_size=72;
}
static bool ui_text(const json_doc_t *d,int root,const char *key,char *out,size_t cap) {
	int t=json_get(d,root,key);
	if(t<0||d->toks[t].type!=JSON_STRING||d->toks[t].end-d->toks[t].start>=(int)cap||
		!json_str(d,t,out,cap)||!out[0])return false;
	for(const unsigned char *p=(const unsigned char *)out;*p;++p)if(*p<32||*p>126)return false;
	// Do not allow embedded escapes, even if decoded to printable characters.
	return !memchr(d->text+d->toks[t].start,'\\',(size_t)(d->toks[t].end-d->toks[t].start));
}
static bool ui_number(const json_doc_t *d,int root,const char *key,unsigned low,unsigned high,unsigned *out) {
	int t=json_get(d,root,key);
	if(t<0||d->toks[t].type!=JSON_NUMBER)return false;
	for(int n=d->toks[t].start;n<d->toks[t].end;++n)
		if(d->text[n]<'0'||d->text[n]>'9')return false;
	long v=json_long(d,t,-1);
	if(v<(long)low||v>(long)high)return false;
	*out=(unsigned)v;return true;
}
static bool ui_flag(const json_doc_t *d,int root,const char *key,bool *out) {
	int t=json_get(d,root,key);
	if(t<0||d->toks[t].type!=JSON_BOOL)return false;
	*out=json_bool(d,t,false);return true;
}
bool store_ui_parse(const char *body,const char *version,store_ui_t *out) {
	long parts[4];store_ui_t ui;
	if(!body||!out||strlen(body)>GAME_STORE_UI_MAX||!version_ok(version,parts))return false;
	store_ui_default(&ui);
	json_doc_t d;if(!json_parse(body,&d))return false;
	int root=json_root(&d);char layout[16],palette[16],declared[SDPACKAGES_VERSION];
	unsigned schema=0;
	bool ok=root>=0&&d.toks[root].type==JSON_OBJECT&&
		ui_number(&d,root,"schema",1,1,&schema)&&
		ui_text(&d,root,"version",declared,sizeof(declared))&&!strcmp(version,declared)&&
		ui_text(&d,root,"heading",ui.heading,sizeof(ui.heading))&&
		ui_text(&d,root,"subtitle",ui.subtitle,sizeof(ui.subtitle))&&
		ui_text(&d,root,"layout",layout,sizeof(layout))&&
		ui_text(&d,root,"palette",palette,sizeof(palette))&&
		ui_number(&d,root,"radius",0,24,&ui.radius)&&
		ui_number(&d,root,"padding",6,16,&ui.padding)&&
		ui_number(&d,root,"gap",4,16,&ui.gap)&&
		ui_number(&d,root,"logo_size",48,88,&ui.logo_size)&&
		ui_flag(&d,root,"emulators_first",&ui.emulators_first)&&
		ui_flag(&d,root,"show_descriptions",&ui.show_descriptions);
	if(ok){
		if(!strcmp(layout,"list"))ui.layout=STORE_UI_LIST;
		else if(!strcmp(layout,"cards"))ui.layout=STORE_UI_CARDS;
		else if(!strcmp(layout,"grid"))ui.layout=STORE_UI_GRID;
		else ok=false;
		if(!strcmp(palette,"system"))ui.palette=STORE_UI_SYSTEM;
		else if(!strcmp(palette,"ocean"))ui.palette=STORE_UI_OCEAN;
		else if(!strcmp(palette,"forest"))ui.palette=STORE_UI_FOREST;
		else if(!strcmp(palette,"amber"))ui.palette=STORE_UI_AMBER;
		else if(!strcmp(palette,"plum"))ui.palette=STORE_UI_PLUM;
		else ok=false;
	}
	json_free(&d);
	if(ok){strcpy(ui.version,version);*out=ui;}
	return ok;
}
bool store_ui_find_asset(const char *catalog,store_ui_asset_t *out) {
	static const char prefix[]="sonix-store-ui-v1-",suffix[]=".json";
	if(!catalog||!out||strlen(catalog)>=GAME_STORE_JSON_MAX)return false;
	memset(out,0,sizeof(*out));
	json_doc_t d;if(!json_parse(catalog,&d))return false;
	int root=json_root(&d),assets=json_get(&d,root,"assets");char tag[64];
	bool ok=root>=0&&assets>=0&&d.toks[assets].type==JSON_ARRAY&&
		json_obj_str(&d,root,"tag_name",tag,sizeof(tag))&&!strcmp(tag,GAME_STORE_TAG)&&
		!json_obj_bool(&d,root,"draft",true);
	for(int i=0;ok&&i<json_len(&d,assets);++i){
		int a=json_at(&d,assets,i);char name[128],state[24],digest[80],url[256],expected[256];
		store_ui_asset_t candidate={0};long parts[4];
		if(!json_obj_str(&d,a,"name",name,sizeof(name)))continue;
		size_t n=strlen(name),p=sizeof(prefix)-1,q=sizeof(suffix)-1;
		if(n<=p+q||strncmp(name,prefix,p)||strcmp(name+n-q,suffix)||n-p-q>=sizeof(candidate.version))continue;
		memcpy(candidate.version,name+p,n-p-q);
		if(!version_ok(candidate.version,parts))continue;
		long size=json_obj_long(&d,a,"size",0);
		int len=snprintf(expected,sizeof(expected),"https://github.com/%s/releases/download/%s/%s",GAME_STORE_REPO,GAME_STORE_TAG,name);
		if(size<=0||size>GAME_STORE_UI_MAX||len<=0||(size_t)len>=sizeof(expected)||
			!json_obj_str(&d,a,"state",state,sizeof(state))||strcmp(state,"uploaded")||
			!json_obj_str(&d,a,"browser_download_url",url,sizeof(url))||strcmp(url,expected)||
			!json_obj_str(&d,a,"digest",digest,sizeof(digest))||!ota_policy_digest(digest))continue;
		if(out->version[0]&&game_store_version_cmp(candidate.version,out->version)<=0)continue;
		candidate.size=(size_t)size;strcpy(candidate.url,expected);strcpy(candidate.sha256,digest+7);
		for(unsigned j=0;j<64;++j)if(candidate.sha256[j]>='A'&&candidate.sha256[j]<='F')candidate.sha256[j]+='a'-'A';
		*out=candidate;
	}
	json_free(&d);return ok&&out->version[0];
}
