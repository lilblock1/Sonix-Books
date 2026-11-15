#ifndef SDPACKAGES_H
#define SDPACKAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SDPACKAGES_VERSION 64
#define SDPACKAGES_MAX_BYTES (256u * 1024u * 1024u)
#define SDPACKAGES_ASSET "Sonix-Books-SD-Packages.spk"
#define SD_EXTRAS_COUNT 3
#define SD_EXTRAS_MASK 7u
// Fixed categories: fonts, dictionaries, native tools.
const char *sdpackages_extra_asset(unsigned category);
const char *sdpackages_extra_folder(unsigned category);
const char *sdpackages_extra_base(unsigned category);
bool sdpackages_extra_generation(const char *root, unsigned category, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]);
// Version of the active managed component, including the legacy combined pack.
bool sdpackages_component_version(const char *root, unsigned category, char version[SDPACKAGES_VERSION]);
typedef bool (*sdpackages_cancel_t)(void *user);
typedef void (*sdpackages_progress_t)(uint64_t done, uint64_t total, void *user);
typedef enum { SDPACKAGES_OK, SDPACKAGES_INVALID, SDPACKAGES_CORRUPT,
	SDPACKAGES_NO_SPACE, SDPACKAGES_IO, SDPACKAGES_CANCELLED } sdpackages_result_t;

// Pure path resolution, shared by fonts, dictionaries and native readers.
// Only a complete managed version is returned; legacy Ebook paths remain
// available to callers for manually installed/custom files.
bool sdpackages_active(const char *root, char version[SDPACKAGES_VERSION]);
bool sdpackages_generation(const char *root, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]);
bool sdpackages_path(const char *root, const char *folder, char *out, size_t size);
bool sdpackages_valid_version(const char *version);
#define SD_GAME_ID_MAX 32
bool sdpackages_game_id(const char *id);
bool sdpackages_game_generation(const char *root, const char *id, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]);
bool sdpackages_game_core(const char *root, const char *id, char *out, size_t size);

// Worker thread only. Downloaded archives must match the trusted release asset
// SHA-256. Extract/verify in a fresh managed directory before the active pointer
// changes. A failed or cancelled install never removes the active version.
sdpackages_result_t sdpackages_install(const char *root, const char *archive,
	const char *version, const char *sha256, sdpackages_cancel_t cancel,
	sdpackages_progress_t progress, void *user);
sdpackages_result_t sdpackages_install_extra(const char *root, unsigned category, const char *archive,
	const char *version, const char *sha256, sdpackages_cancel_t cancel, sdpackages_progress_t progress, void *user);

sdpackages_result_t sdpackages_install_game(const char *root, const char *id, const char *archive,
	const char *version, const char *sha256, sdpackages_cancel_t cancel, sdpackages_progress_t progress, void *user);

#endif
