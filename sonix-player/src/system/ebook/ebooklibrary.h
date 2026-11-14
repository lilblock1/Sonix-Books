#ifndef EBOOKLIBRARY_H
#define EBOOKLIBRARY_H

#include <stdbool.h>
#include <stdint.h>

/* Bounded results, independent of LVGL. Scanning belongs on a worker thread. */
#define EBOOKLIBRARY_MAX 300
#define EBOOKLIBRARY_PATH 800
#define EBOOKLIBRARY_DEPTH 16
#define EBOOKLIBRARY_VISITS 10000

typedef enum {
	EBOOKLIBRARY_TITLE,
	EBOOKLIBRARY_TITLE_DESC,
	EBOOKLIBRARY_AUTHOR,
	EBOOKLIBRARY_NEWEST,
	EBOOKLIBRARY_OLDEST,
	EBOOKLIBRARY_SORT_COUNT
} ebooklibrary_sort_t;

typedef struct {
	char path[EBOOKLIBRARY_PATH]; /* full path, including subfolders */
	char title[256];
	char author[128];
	int64_t modified;
	bool directory;
} ebooklibrary_entry_t;

typedef struct {
	ebooklibrary_entry_t entries[EBOOKLIBRARY_MAX];
	int count;
	bool truncated; /* result, depth, path or traversal limit reached */
	bool cancelled;
	bool unavailable;
	unsigned unreadable;
} ebooklibrary_result_t;

typedef bool (*ebooklibrary_cancel_cb)(void *user);

/* folder is relative to root ("" means root). Rejects absolute paths, dot
 * components, backslashes and symlinks. Empty query lists the current folder;
 * a nonempty query searches title, author and filename throughout root.
 * cache_dir may be NULL; a missing/read-only cache never prevents reading.
 * Results are deterministic, folders first; excess results are reported.
 */
void ebooklibrary_scan(const char *root, const char *folder, const char *query,
					   ebooklibrary_sort_t sort, const char *cache_dir,
					   ebooklibrary_result_t *out, ebooklibrary_cancel_cb cancel, void *user);

/* Same traversal rules, without parsing metadata. Used by Continue Reading. */
bool ebooklibrary_find(const char *root, bool (*match)(const char *, void *), void *user,
					   char *out, unsigned size, ebooklibrary_cancel_cb cancel);

#endif
