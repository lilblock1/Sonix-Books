#include "src/system/device/game_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DIGEST "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define URL "https://github.com/" GAME_STORE_REPO "/releases/download/" GAME_STORE_TAG "/"
#define ASSET(name,url) "{\"name\":\"" name "\",\"size\":1234,\"state\":\"uploaded\",\"browser_download_url\":\"" url "\",\"digest\":\"" DIGEST "\"}"

int main(void) {
	game_store_item_t items[8];
	size_t count=0;
	assert(game_store_parse("{\"tag_name\":\"" GAME_STORE_TAG "\",\"draft\":false,\"assets\":[" 
		ASSET("sonix-game-v1-2048-v1.0.0.spk",URL "sonix-game-v1-2048-v1.0.0.spk") ","
		ASSET("sonix-game-v1-2048-v1.10.0.spk",URL "sonix-game-v1-2048-v1.10.0.spk") ","
		ASSET("sonix-game-v1-2048-v1.2.0.spk",URL "sonix-game-v1-2048-v1.2.0.spk") ","
		ASSET("sonix-game-v1-nes-v1.0.0.spk","https://example.com/untrusted.spk") ","
		ASSET("sonix-game-v2-nes-v1.0.0.spk",URL "sonix-game-v2-nes-v1.0.0.spk") ","
		ASSET("sonix-game-v1-gba-v2.0.0.spk",URL "sonix-game-v1-gba-v2.0.0.spk")
		"]}",items,8,&count));
	assert(count==2 && !strcmp(items[0].id,"2048") && !strcmp(items[0].version,"v1.10.0"));
	assert(!strcmp(items[1].id,"gba"));
	assert(items[0].kind==GAME_STORE_GAME&&!strcmp(items[0].title,"Sonix 2048"));
	assert(items[1].kind==GAME_STORE_EMULATOR);
	assert(game_store_profile("2048")->no_rom&&!game_store_profile("gba")->no_rom);
	assert(!game_store_profile("unknown-title"));
	assert(game_store_parse("{\"tag_name\":\"" GAME_STORE_TAG "\",\"draft\":false,\"assets\":["
		"{\"name\":\"sonix-game-v1-new-title-v1.0.0.spk\",\"size\":1234,\"state\":\"uploaded\","
		"\"browser_download_url\":\"" URL "sonix-game-v1-new-title-v1.0.0.spk\",\"digest\":\"" DIGEST "\","
		"\"label\":\"sonix-ui1|game|New Title|A simple puzzle.\"}]}",items,8,&count));
	assert(count==1&&items[0].kind==GAME_STORE_GAME&&!strcmp(items[0].title,"New Title"));
	assert(!strcmp(items[0].description,"A simple puzzle."));
	assert(game_store_parse("{\"tag_name\":\"" GAME_STORE_TAG "\",\"draft\":false,\"assets\":["
		ASSET("sonix-game-v1-2048-v1.1.0.spk",URL "sonix-game-v1-2048-v1.1.0.spk") ","
		"{\"name\":\"sonix-logo-v1-2048-v1.1.0.rgb565\",\"size\":4608,\"state\":\"uploaded\","
		"\"browser_download_url\":\"https://example.com/wrong.rgb565\",\"digest\":\"" DIGEST "\"},"
		"{\"name\":\"sonix-logo-v1-2048-v1.0.0.rgb565\",\"size\":4608,\"state\":\"uploaded\","
		"\"browser_download_url\":\"" URL "sonix-logo-v1-2048-v1.0.0.rgb565\",\"digest\":\"" DIGEST "\"},"
		"{\"name\":\"sonix-logo-v1-2048-v1.1.0.rgb565\",\"size\":4607,\"state\":\"uploaded\","
		"\"browser_download_url\":\"" URL "sonix-logo-v1-2048-v1.1.0.rgb565\",\"digest\":\"" DIGEST "\"},"
		"{\"name\":\"sonix-logo-v1-2048-v1.1.0.rgb565\",\"size\":4608,\"state\":\"uploaded\","
		"\"browser_download_url\":\"" URL "sonix-logo-v1-2048-v1.1.0.rgb565\",\"digest\":\"" DIGEST "\"}]}",items,8,&count));
	assert(count==1&&!strcmp(items[0].logo_url,URL "sonix-logo-v1-2048-v1.1.0.rgb565"));
	assert(!strcmp(items[0].logo_sha256,DIGEST+7));
	assert(!game_store_parse("{\"tag_name\":\"v0.3.0.13\",\"draft\":false,\"assets\":[]}",items,8,&count));
	assert(!game_store_parse("{\"tag_name\":\"" GAME_STORE_TAG "\",\"draft\":true,\"assets\":[]}",items,8,&count));
	assert(game_store_version_cmp("v1.10.0","v1.2.0")>0);
	assert(game_store_version_cmp("v1.0.0","v2.0.0")<0);
	assert(!sdpackages_game_id("../bad") && !sdpackages_game_id("bad/path") && sdpackages_game_id("gba"));
	store_ui_t ui;store_ui_default(&ui);assert(ui.layout==STORE_UI_LIST&&ui.logo_size==72);
	const char *design="{\"schema\":1,\"version\":\"v1.0.0\",\"heading\":\"Sonix Game Store\","
		"\"subtitle\":\"Discover games and emulators\",\"layout\":\"grid\",\"palette\":\"ocean\","
		"\"radius\":16,\"padding\":10,\"gap\":8,\"logo_size\":76,\"emulators_first\":false,\"show_descriptions\":true}";
	assert(store_ui_parse(design,"v1.0.0",&ui)&&ui.layout==STORE_UI_GRID&&ui.palette==STORE_UI_OCEAN&&ui.logo_size==76);
	assert(!store_ui_parse(design,"v1.0.1",&ui)&&!strcmp(ui.version,"v1.0.0"));
	assert(!store_ui_parse("{\"schema\":2}","v1.0.0",&ui)&&ui.logo_size==76);
	assert(!store_ui_parse("{\"schema\":1,\"logo_size\":9999}","v1.0.0",&ui));
	store_ui_asset_t appearance;
	assert(store_ui_find_asset("{\"tag_name\":\"" GAME_STORE_TAG "\",\"draft\":false,\"assets\":["
		ASSET("sonix-store-ui-v1-v1.0.0.json",URL "sonix-store-ui-v1-v1.0.0.json") ","
		ASSET("sonix-store-ui-v1-v1.1.0.json",URL "sonix-store-ui-v1-v1.1.0.json") ","
		ASSET("sonix-store-ui-v2-v9.0.0.json",URL "sonix-store-ui-v2-v9.0.0.json") "]}",&appearance));
	assert(!strcmp(appearance.version,"v1.1.0")&&appearance.size==1234&&!strcmp(appearance.sha256,DIGEST+7));
	puts("Game Store catalog checks passed");
	return 0;
}
