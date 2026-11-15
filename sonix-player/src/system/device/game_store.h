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

typedef struct {
	char id[SD_GAME_ID_MAX];
	char version[SDPACKAGES_VERSION];
	char url[256], sha256[65];
	long size;
} game_store_item_t;

// GitHub release metadata is the catalog. No arbitrary URLs, package paths,
// executable names or commands are accepted from the server.
bool game_store_parse(const char *json, game_store_item_t *items, size_t capacity, size_t *count);
int game_store_version_cmp(const char *a, const char *b);
#endif
