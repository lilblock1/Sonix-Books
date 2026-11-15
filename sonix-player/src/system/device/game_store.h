#ifndef GAME_STORE_H
#define GAME_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include "sdpackages.h"

#define GAME_STORE_REPO "lilblock1/Sonix-Books"
#define GAME_STORE_TAG "sonix-game-store-v1"
#define GAME_STORE_MAX 48
#define GAME_STORE_JSON_MAX (256u * 1024u)
#define GAME_STORE_ABI 1
#define GAME_LOGO_SIDE 48u
#define GAME_LOGO_BYTES (GAME_LOGO_SIDE*GAME_LOGO_SIDE*2u)

typedef enum { GAME_STORE_OTHER=0, GAME_STORE_GAME, GAME_STORE_EMULATOR } game_store_kind_t;
typedef struct {
	const char *title, *description;
	game_store_kind_t kind;
	bool no_rom;
} game_store_profile_t;

// Local descriptions also work for the original assets without labels.
// Unknown IDs have neutral placeholders and stay in the generic ROM picker.
const game_store_profile_t *game_store_profile(const char *id);

typedef struct {
	char id[SD_GAME_ID_MAX];
	char version[SDPACKAGES_VERSION];
	char url[256], sha256[65];
	long size;
	char title[64], description[160];
	game_store_kind_t kind;
	char logo_url[256], logo_sha256[65];
} game_store_item_t;

// GitHub release metadata is the catalog. No arbitrary URLs, package paths,
// executable names or commands are accepted from the server.
bool game_store_parse(const char *json, game_store_item_t *items, size_t capacity, size_t *count);
int game_store_version_cmp(const char *a, const char *b);
#endif
