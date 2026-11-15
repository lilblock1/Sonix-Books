#define _POSIX_C_SOURCE 200809L
#include "ebooklibrary.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#define lstat stat
#endif

#include "ebook.h"
#include "ebooktext.h"

/* Internal ordering; the five user-selectable library sorts stay unchanged. */
#define SORT_RECENT ((ebooklibrary_sort_t)EBOOKLIBRARY_SORT_COUNT)

static bool ordinary_stat(const char *path, struct stat *st) {
#ifdef _WIN32
	DWORD attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
	return lstat(path, st) == 0 && (S_ISREG(st->st_mode) || S_ISDIR(st->st_mode));
}

static bool epub_name(const char *name) {
	return ebook_supported(name);
}

static void copy_text(char *out, size_t size, const char *text) {
	if (!text) text = "";
	size_t n = strlen(text);
	if (n >= size) {
		n = size - 1;
		while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
	}
	memcpy(out, text, n);
	out[n] = 0;
	for (size_t i = 0; i < n; i++) if ((unsigned char)out[i] < 32) out[i] = ' ';
}

static uint32_t next_folded(const char **p) { return ebooktext_next_folded(p); }

static int text_compare(const char *a, const char *b) {
	while (*a && *b) {
		uint32_t x = next_folded(&a), y = next_folded(&b);
		if (x != y) return x < y ? -1 : 1;
	}
	return !!*a - !!*b;
}

static bool contains(const char *text, const char *query) {
	if (!query[0]) return true;
	for (const char *start = text; *start; next_folded(&start)) {
		const char *a = start, *b = query;
		while (*a && *b) {
			if (next_folded(&a) != next_folded(&b)) break;
			if (!*b) return true;
		}
	}
	return false;
}

static int compare(const ebooklibrary_entry_t *a, const ebooklibrary_entry_t *b, ebooklibrary_sort_t sort) {
	if (a->directory != b->directory) return a->directory ? -1 : 1;
	if (sort == SORT_RECENT && a->recent_rank != b->recent_rank)
		return a->recent_rank < b->recent_rank ? -1 : 1;
	int c = 0;
	if (!a->directory) {
		if (sort == EBOOKLIBRARY_AUTHOR) {
			/* Unknown authors follow named authors. */
			c = !a->author[0] - !b->author[0];
			if (!c) c = text_compare(a->author, b->author);
		} else if (sort == EBOOKLIBRARY_NEWEST || sort == EBOOKLIBRARY_OLDEST) {
			c = (a->modified > b->modified) - (a->modified < b->modified);
			if (sort == EBOOKLIBRARY_NEWEST) c = -c;
		}
	}
	if (!c) {
		c = text_compare(a->title, b->title);
		if (!a->directory && sort == EBOOKLIBRARY_TITLE_DESC) c = -c;
	}
	return c ? c : strcmp(a->path, b->path);
}

/* Keep the best MAX entries in a max heap. A matching book on a large card
 * costs O(log MAX) comparisons, rather than shifting hundreds of full paths. */
static void sift_down(ebooklibrary_entry_t *entries, int count, int at, ebooklibrary_sort_t sort) {
	ebooklibrary_entry_t entry = entries[at];
	for (int child = at * 2 + 1; child < count; child = at * 2 + 1) {
		if (child + 1 < count && compare(&entries[child], &entries[child + 1], sort) < 0) child++;
		if (compare(&entry, &entries[child], sort) >= 0) break;
		entries[at] = entries[child];
		at = child;
	}
	entries[at] = entry;
}

static void insert(ebooklibrary_result_t *out, const ebooklibrary_entry_t *entry, ebooklibrary_sort_t sort) {
	if (out->count == EBOOKLIBRARY_MAX) {
		out->truncated = true;
		if (compare(entry, &out->entries[0], sort) >= 0) return;
		out->entries[0] = *entry;
		sift_down(out->entries, out->count, 0, sort);
		return;
	}
	int at = out->count++;
	while (at > 0) {
		int parent = (at - 1) / 2;
		if (compare(&out->entries[parent], entry, sort) >= 0) break;
		out->entries[at] = out->entries[parent];
		at = parent;
	}
	out->entries[at] = *entry;
}

static void sort_results(ebooklibrary_result_t *out, ebooklibrary_sort_t sort) {
	for (int end = out->count - 1; end > 0; end--) {
		ebooklibrary_entry_t swap = out->entries[0];
		out->entries[0] = out->entries[end];
		out->entries[end] = swap;
		sift_down(out->entries, end, 0, sort);
	}
}

typedef struct {
	uint32_t version;
	int64_t size, modified;
	char path[EBOOKLIBRARY_PATH], title[256], author[128];
} metadata_cache_t;

static void metadata(ebooklibrary_entry_t *entry, const struct stat *st, const char *cache_dir) {
	char cache_path[1024] = "";
	metadata_cache_t cache = {0};
	if (cache_dir && cache_dir[0]) {
		uint64_t hash = 14695981039346656037ull;
		for (const unsigned char *p = (const unsigned char *)entry->path; *p; p++) hash = (hash ^ *p) * 1099511628211ull;
		int n = snprintf(cache_path, sizeof(cache_path), "%s/%016llx.meta", cache_dir, (unsigned long long)hash);
		if (n < 0 || (size_t)n >= sizeof(cache_path)) cache_path[0] = 0;
		FILE *f = cache_path[0] ? fopen(cache_path, "rb") : NULL;
		if (f) {
			bool ok = fread(&cache, sizeof(cache), 1, f) == 1;
			fclose(f);
			if (ok && cache.version == 1 && cache.size == (int64_t)st->st_size &&
				cache.modified == (int64_t)st->st_mtime && cache.path[sizeof(cache.path) - 1] == 0 &&
				cache.title[sizeof(cache.title) - 1] == 0 && cache.author[sizeof(cache.author) - 1] == 0 &&
				strcmp(cache.path, entry->path) == 0) {
				copy_text(entry->title, sizeof(entry->title), cache.title);
				copy_text(entry->author, sizeof(entry->author), cache.author);
				return;
			}
		}
	}
	if (ebook_format(entry->path) != EBOOK_FORMAT_EPUB) return;
	ebook_t *book = ebook_open(entry->path);
	if (!book) return; /* Keep filename; retry a damaged/transiently unreadable file next time. */
	if (ebook_title(book)[0]) copy_text(entry->title, sizeof(entry->title), ebook_title(book));
	copy_text(entry->author, sizeof(entry->author), ebook_author(book));
	ebook_close(book);
	if (!cache_path[0]) return;
	memset(&cache, 0, sizeof(cache));
	cache.version = 1;
	cache.size = (int64_t)st->st_size;
	cache.modified = (int64_t)st->st_mtime;
	copy_text(cache.path, sizeof(cache.path), entry->path);
	copy_text(cache.title, sizeof(cache.title), entry->title);
	copy_text(cache.author, sizeof(cache.author), entry->author);
	char tmp[1032];
	snprintf(tmp, sizeof(tmp), "%s.tmp", cache_path);
	FILE *f = fopen(tmp, "wb");
	if (f) {
		bool ok = fwrite(&cache, sizeof(cache), 1, f) == 1;
		if (fclose(f) != 0) ok = false;
		if (!ok || rename(tmp, cache_path) != 0) remove(tmp);
	}
}

typedef struct {
	const char *query, *cache_dir;
	ebooklibrary_sort_t sort;
	ebooklibrary_result_t *out;
	ebooklibrary_cancel_cb cancel;
	void *user;
	unsigned visits;
	bool recursive;
	bool (*match)(const char *, void *);
	int (*rank)(const char *, void *);
	char *found;
	unsigned found_size;
} scan_t;

static bool stopped(scan_t *s) {
	if (s->cancel && s->cancel(s->user)) s->out->cancelled = true;
	return s->out->cancelled || (s->found && s->found[0]);
}

static void walk(scan_t *s, const char *folder, unsigned depth) {
	if (stopped(s)) return;
	DIR *d = opendir(folder);
	if (!d) { s->out->unreadable++; return; }
	struct dirent *de;
	while (!stopped(s) && (de = readdir(d)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		if (++s->visits > EBOOKLIBRARY_VISITS) { s->out->truncated = true; break; }
		if (de->d_name[0] == '.') continue;
		/* A single reusable stack entry per depth, never a whole book per file. */
		ebooklibrary_entry_t entry = {0};
		int n = snprintf(entry.path, sizeof(entry.path), "%s/%s", folder, de->d_name);
		if (n < 0 || (size_t)n >= sizeof(entry.path)) { s->out->truncated = true; continue; }
		struct stat st;
		if (!ordinary_stat(entry.path, &st)) continue;
		entry.directory = S_ISDIR(st.st_mode);
		entry.modified = (int64_t)st.st_mtime;
		copy_text(entry.title, sizeof(entry.title), de->d_name);
		if (entry.directory) {
			if (!s->recursive) insert(s->out, &entry, s->sort);
			else if (depth < EBOOKLIBRARY_DEPTH) walk(s, entry.path, depth + 1);
			else s->out->truncated = true;
		} else if (epub_name(de->d_name)) {
			if (s->match) {
				if (s->match(entry.path, s->user) && strlen(entry.path) < s->found_size) strcpy(s->found, entry.path);
				continue;
			}
			if (s->rank) {
				entry.recent_rank = s->rank(entry.path, s->user);
				if (entry.recent_rank < 0) continue;
			}
			char *extension = strrchr(entry.title, '.');
			if (extension) *extension = 0;
			metadata(&entry, &st, s->cache_dir);
			if (stopped(s)) break;
			if (contains(entry.title, s->query) || contains(entry.author, s->query) || contains(de->d_name, s->query))
				insert(s->out, &entry, s->sort);
		}
	}
	closedir(d);
}

/* Resolve one component at a time. A path prefix check alone would allow a
 * symlink in the middle of a nested path to leave Ebook. */
static bool resolve(const char *root, const char *folder, char *out) {
	if (!root || !root[0] || !folder || strlen(root) >= EBOOKLIBRARY_PATH) return false;
	strcpy(out, root);
	struct stat st;
	if (!ordinary_stat(out, &st) || !S_ISDIR(st.st_mode)) return false;
	const char *p = folder;
	while (*p) {
		const char *end = strchr(p, '/');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (!len || (len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return false;
		for (size_t i = 0; i < len; i++) if (p[i] == '\\' || p[i] == ':') return false;
		size_t used = strlen(out);
		if (used + len + 2 > EBOOKLIBRARY_PATH) return false;
		out[used] = '/';
		memcpy(out + used + 1, p, len);
		out[used + len + 1] = 0;
		if (!ordinary_stat(out, &st) || !S_ISDIR(st.st_mode)) return false;
		if (!end) break;
		p = end + 1;
		if (!*p) return false;
	}
	return true;
}

void ebooklibrary_scan(const char *root, const char *folder, const char *query, ebooklibrary_sort_t sort,
					   const char *cache_dir, ebooklibrary_result_t *out, ebooklibrary_cancel_cb cancel, void *user) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	char dir[EBOOKLIBRARY_PATH];
	bool recursive = query && query[0];
	if (!resolve(root, recursive ? "" : folder, dir)) { out->unavailable = true; return; }
	if (sort < 0 || sort >= EBOOKLIBRARY_SORT_COUNT) sort = EBOOKLIBRARY_TITLE;
	scan_t scan = {.query = query ? query : "", .cache_dir = cache_dir, .sort = sort, .out = out,
		.cancel = cancel, .user = user, .recursive = recursive};
	walk(&scan, dir, 0);
	sort_results(out, sort);
	if (out->unreadable && !out->count) out->unavailable = true;
}

void ebooklibrary_recent(const char *root, int (*rank)(const char *, void *), void *user,
						 const char *query, const char *cache_dir, ebooklibrary_result_t *out,
						 ebooklibrary_cancel_cb cancel) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	char dir[EBOOKLIBRARY_PATH];
	if (!rank || !resolve(root, "", dir)) { out->unavailable = true; return; }
	scan_t scan = {.query = query ? query : "", .cache_dir = cache_dir, .sort = SORT_RECENT,
		.out = out, .cancel = cancel, .user = user, .recursive = true, .rank = rank};
	walk(&scan, dir, 0);
	sort_results(out, SORT_RECENT);
	if (out->unreadable && !out->count) out->unavailable = true;
}

bool ebooklibrary_find(const char *root, bool (*match)(const char *, void *), void *user,
					   char *out, unsigned size, ebooklibrary_cancel_cb cancel) {
	if (!out || !size) return false;
	out[0] = 0;
	char dir[EBOOKLIBRARY_PATH];
	if (!match || !resolve(root, "", dir)) return false;
	/* Find doesn't need the half-megabyte result array. Only status flags from
 * the walker are needed, so allocate off the input/UI thread stack. */
	ebooklibrary_result_t *result = calloc(1, sizeof(*result));
	if (!result) return false;
	scan_t scan = {.out = result, .recursive = true, .match = match, .found = out, .found_size = size,
		.cancel = cancel, .user = user};
	walk(&scan, dir, 0);
	free(result);
	return out[0] != 0;
}
