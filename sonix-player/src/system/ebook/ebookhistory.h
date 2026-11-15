#ifndef EBOOKHISTORY_H
#define EBOOKHISTORY_H

#include <stdbool.h>
#include <stddef.h>

#define EBOOKHISTORY_MAX 12
#define EBOOKHISTORY_KEY_SIZE 17

typedef struct {
	int count;
	char keys[EBOOKHISTORY_MAX][EBOOKHISTORY_KEY_SIZE];
} ebookhistory_list_t;

// Stores only the existing bookmark path hash, so long filenames do not get
// truncated by the settings store. Reading positions keep their original keys.
void ebookhistory_remember(const char *path);
bool ebookhistory_is_last(const char *path);

// UI thread: snapshot the bounded, deduplicated most-recent-first list. The
// legacy last_book is merged first, including after using an older firmware.
void ebookhistory_read(ebookhistory_list_t *out);

// Pure lookup against a snapshot; safe on the library worker. -1 if absent.
int ebookhistory_rank(const ebookhistory_list_t *list, const char *path);

// Legacy resolver for a flat folder. The GUI uses the library's recursive
// recent resolver. No book is parsed and only current files are considered.
bool ebookhistory_find_last(const char *folder, char *out, size_t out_size);

#endif
