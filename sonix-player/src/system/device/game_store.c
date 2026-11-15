#include "game_store.h"
#include "ota_policy.h"
#include "src/system/core/json.h"
#include <stdio.h>
#include <string.h>

#define PREFIX "sonix-game-v1-"
#define SUFFIX ".spk"

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
		memcpy(item.sha256,digest+7,65);
		for (int j=0; item.sha256[j]; ++j) if (item.sha256[j]>='A' && item.sha256[j]<='F') item.sha256[j]+='a'-'A';
		size_t k=0;
		while(k<*count && strcmp(items[k].id,item.id)) ++k;
		if(k<*count && game_store_version_cmp(item.version,items[k].version)<=0) continue;
		if(k==*count) { if(*count>=capacity) {ok=false;break;} ++*count; }
		items[k]=item;
	}
	json_free(&doc);
	if(!ok) *count=0;
	return ok;
}
