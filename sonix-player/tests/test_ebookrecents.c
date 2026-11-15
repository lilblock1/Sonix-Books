#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/core/config.h"
#include "src/system/ebook/bookmarks.h"
#include "src/system/ebook/ebookhistory.h"

static char config_file[512];

static void remember(const char *path) {
	ebookhistory_remember(path);
#ifdef _WIN32
	/* Native Windows CRT rename cannot replace an existing file. The device
	 * uses Linux's existing atomic config writer, which is unchanged. Retry a
	 * dirty save after removing only the test destination. An unchanged MRU
	 * produces no write and must retain its existing file. */
	if (!config_store_save(config_ebook_store())) {
		remove(config_file);
		assert(config_store_save(config_ebook_store()));
	}
#endif
	assert(config_store_init(config_ebook_store(), config_file));
}

static void book_path(char *out, size_t size, int number) {
	snprintf(out, size, "/card/Ebook/Folder/Book%02d.epub", number);
}

static int saved_entries(void) {
	FILE *f = fopen(config_file, "r"); assert(f);
	char line[256]; int count = 0;
	while (fgets(line, sizeof(line), f)) if (strchr(line, '=')) count++;
	fclose(f);
	return count;
}

int main(int argc, char **argv) {
	assert(argc == 2);
	assert(snprintf(config_file, sizeof(config_file), "%s/ebook.ini", argv[1]) < (int)sizeof(config_file));
	config_store_t *store = config_ebook_store();
	(void)config_store_init(store, config_file);
	ebookhistory_list_t list;
	ebookhistory_read(&list);
	assert(!list.count);
	assert(ebookhistory_rank(&list, "missing") == -1);
	assert(ebookhistory_rank(NULL, "missing") == -1);
	ebookhistory_read(NULL);
	ebookhistory_remember(NULL);
	ebookhistory_remember("");

	/* Upgrade from a working last_book and existing reading preferences. */
	const char *legacy = "/card/Ebook/Old/Legacy.epub";
	char hash[24]; bookmark_key(legacy, hash, sizeof(hash));
	config_store_set(store, "ebook", "last_book", hash);
	config_store_set(store, "ebook", "pos_existing", "3:20:17|p1:37:0");
	config_store_set_int(store, "ebook", "font_size", 26);
	ebookhistory_read(&list);
	assert(list.count == 1 && ebookhistory_rank(&list, legacy) == 0);
	assert(ebookhistory_is_last(legacy));
	remember("/card/Ebook/New/New.epub");
	ebookhistory_read(&list);
	assert(list.count == 2 && ebookhistory_rank(&list, legacy) == 1);
	assert(ebookhistory_rank(&list, "/card/Ebook/New/New.epub") == 0);
	assert(config_store_get_int(store, "ebook", "font_size", 0) == 26);
	assert(!strcmp(config_store_get(store, "ebook", "pos_existing", ""), "3:20:17|p1:37:0"));
	assert(saved_entries() == 5); /* two fixed history chunks, last_book, position, preference */

	/* The list stays bounded and most-recent-first across repeated reloads. */
	char path[800];
	for (int i = 0; i < EBOOKHISTORY_MAX + 3; i++) {
		book_path(path, sizeof(path), i);
		remember(path);
	}
	ebookhistory_read(&list);
	assert(list.count == EBOOKHISTORY_MAX);
	for (int i = 0; i < EBOOKHISTORY_MAX; i++) {
		book_path(path, sizeof(path), EBOOKHISTORY_MAX + 2 - i);
		assert(ebookhistory_rank(&list, path) == i);
	}
	book_path(path, sizeof(path), 2);
	assert(ebookhistory_rank(&list, path) == -1);
	assert(saved_entries() == 5);
	assert(strlen(config_store_get(store, "ebook", "recent_v1_0", "")) == 101);
	assert(strlen(config_store_get(store, "ebook", "recent_v1_1", "")) == 101);

	book_path(path, sizeof(path), 5);
	remember(path);
	ebookhistory_read(&list);
	assert(list.count == EBOOKHISTORY_MAX && ebookhistory_rank(&list, path) == 0);
	assert(ebookhistory_is_last(path));
	book_path(path, sizeof(path), 14);
	assert(ebookhistory_rank(&list, path) == 1);
	/* Reopening the first entry doesn't create a duplicate. */
	book_path(path, sizeof(path), 5);
	remember(path);
	ebookhistory_read(&list);
	assert(list.count == EBOOKHISTORY_MAX && ebookhistory_rank(&list, path) == 0);

	/* A worker snapshot is immutable when the UI remembers another book. */
	ebookhistory_list_t snapshot = list;
	remember("/card/Ebook/Other/Book05.epub");
	ebookhistory_read(&list);
	assert(ebookhistory_rank(&list, "/card/Ebook/Other/Book05.epub") == 0);
	assert(ebookhistory_rank(&list, "/card/Ebook/Folder/Book05.epub") == 1);
	assert(ebookhistory_rank(&snapshot, "/card/Ebook/Other/Book05.epub") == -1);
	assert(ebookhistory_rank(&snapshot, "/card/Ebook/Folder/Book05.epub") == 0);

	/* A temporary rollback to old firmware only updates last_book. Merge it. */
	bookmark_key(legacy, hash, sizeof(hash));
	config_store_set(store, "ebook", "last_book", hash);
	ebookhistory_read(&list);
	assert(list.count == EBOOKHISTORY_MAX && ebookhistory_rank(&list, legacy) == 0);
	assert(ebookhistory_rank(&list, "/card/Ebook/Other/Book05.epub") == 1);

	/* Corrupted chunks are bounded and do not hide later valid keys. */
	char first[24], second[24], upper[24], value[128];
	bookmark_key("/a.epub", first, sizeof(first));
	bookmark_key("/b.epub", second, sizeof(second));
	snprintf(upper, sizeof(upper), "%s", first);
	for (size_t i = 0; upper[i]; i++) upper[i] = (char)toupper((unsigned char)upper[i]);
	snprintf(value, sizeof(value), "bad,,%s,%s,not-a-hash,", upper, second);
	config_store_set(store, "ebook", "last_book", "broken");
	config_store_set(store, "ebook", "recent_v1_0", value);
	snprintf(value, sizeof(value), "%s,%s", first, second);
	config_store_set(store, "ebook", "recent_v1_1", value);
	ebookhistory_read(&list);
	assert(list.count == 2);
	assert(ebookhistory_rank(&list, "/a.epub") == 0 && ebookhistory_rank(&list, "/b.epub") == 1);
	assert(ebookhistory_is_last("/a.epub"));
	list.count = INT_MAX;
	assert(ebookhistory_rank(&list, "/absent.epub") == -1);
	assert(ebookhistory_rank(&list, NULL) == -1);
	assert(ebookhistory_rank(&list, "") == -1);

	/* Long paths never enter the short-value settings store. */
	char long_path[700]; memset(long_path, 'x', sizeof(long_path));
	memcpy(long_path, "/card/Ebook/", 12);
	memcpy(long_path + sizeof(long_path) - 6, ".epub", 6);
	remember(long_path);
	ebookhistory_read(&list);
	assert(ebookhistory_rank(&list, long_path) == 0 && list.count == 3);
	assert(config_store_get_int(store, "ebook", "font_size", 0) == 26);
	assert(saved_entries() == 5);
	puts("ebook recents: ordering, persistence, deduplication, cap, migration, snapshots and malformed data passed");
	return 0;
}
