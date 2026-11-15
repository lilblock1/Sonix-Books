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
	assert(!game_store_parse("{\"tag_name\":\"v0.3.0.13\",\"draft\":false,\"assets\":[]}",items,8,&count));
	assert(!game_store_parse("{\"tag_name\":\"" GAME_STORE_TAG "\",\"draft\":true,\"assets\":[]}",items,8,&count));
	assert(game_store_version_cmp("v1.10.0","v1.2.0")>0);
	assert(game_store_version_cmp("v1.0.0","v2.0.0")<0);
	assert(!sdpackages_game_id("../bad") && !sdpackages_game_id("bad/path") && sdpackages_game_id("gba"));
	puts("Game Store catalog checks passed");
	return 0;
}
