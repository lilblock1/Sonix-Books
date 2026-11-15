#ifndef EBOOKDICT_H
#define EBOOKDICT_H

#include <stdbool.h>
#include <stddef.h>

#define EBOOKDICT_MAX 8
#define EBOOKDICT_ID 64
#define EBOOKDICT_WORD 96
#define EBOOKDICT_QUERY 256
#define EBOOKDICT_TEXT 8192
#define EBOOKDICT_MAX_FILE (128u * 1024u * 1024u)
#define EBOOKDICT_MAX_ENTRIES 500000u
#define EBOOKDICT_MAX_DEFINITION 65536u

typedef bool (*ebookdict_cancel_t)(void *user);
typedef struct { char id[EBOOKDICT_ID], name[EBOOKDICT_ID]; } ebookdict_entry_t;
typedef struct {
	ebookdict_entry_t entries[EBOOKDICT_MAX];
	unsigned count, skipped;
	bool unavailable, truncated, cancelled;
} ebookdict_catalog_t;
typedef enum {
	EBOOKDICT_OK, EBOOKDICT_NOT_FOUND, EBOOKDICT_EMPTY, EBOOKDICT_LONG_QUERY,
	EBOOKDICT_BAD_QUERY, EBOOKDICT_UNAVAILABLE, EBOOKDICT_BAD_FILE, EBOOKDICT_CANCELLED
} ebookdict_status_t;
typedef struct {
	ebookdict_status_t status;
	char word[EBOOKDICT_WORD], text[EBOOKDICT_TEXT + 1];
	unsigned probes;
	bool truncated;
} ebookdict_result_t;

// Worker-thread operations. No settings, book handles, LVGL or network access.
// Flat .sxd files below SD/Ebook/Dictionaries; links/traversal are rejected.
void ebookdict_scan(const char *sd_root, ebookdict_catalog_t *out, ebookdict_cancel_t cancel, void *user);
ebookdict_status_t ebookdict_normalize(const char *query, char out[EBOOKDICT_WORD]);
void ebookdict_lookup(const char *sd_root, const char *id, const char *query,
	ebookdict_result_t *out, ebookdict_cancel_t cancel, void *user);

#endif
