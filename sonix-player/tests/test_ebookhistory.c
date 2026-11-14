#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/system/core/config.h"
#include "src/system/ebook/ebookhistory.h"

static void create_file(const char *path) {
	FILE *file = fopen(path, "w");
	assert(file);
	fputs("test fixture; no EPUB parsing is performed by history", file);
	assert(fclose(file) == 0);
}

int main(void) {
	char temp[] = "/tmp/sonix-books-history-XXXXXX";
	assert(mkdtemp(temp));
	char config[512], folder[512], other[512], book[800], longbook[800], directory[800], out[800];
	snprintf(config, sizeof(config), "%s/ebook.ini", temp);
	snprintf(folder, sizeof(folder), "%s/Ebook", temp);
	snprintf(other, sizeof(other), "%s/other-card", temp);
	assert(mkdir(folder, 0700) == 0);
	assert(mkdir(other, 0700) == 0);
	config_store_t *store = config_ebook_store();
	config_store_init(store, config);
	snprintf(book, sizeof(book), "%s/Book.EPUB", folder);
	create_file(book);
	assert(!ebookhistory_find_last(folder, out, sizeof(out)) && out[0] == '\0');
	assert(!ebookhistory_is_last(NULL));
	ebookhistory_remember(NULL);

	// A saved selection survives a full settings reload without changing the
	// existing per-book positions or reading preferences.
	config_store_set(store, "ebook", "pos_existing", "3:14:8");
	config_store_set_int(store, "ebook", "font_size", 30);
	ebookhistory_remember(book);
	assert(ebookhistory_is_last(book));
	assert(config_store_init(store, config));
	assert(ebookhistory_find_last(folder, out, sizeof(out)));
	assert(strcmp(out, book) == 0);
	assert(strcmp(config_store_get(store, "ebook", "pos_existing", ""), "3:14:8") == 0);
	assert(config_store_get_int(store, "ebook", "font_size", 0) == 30);

	// A path longer than the config store's 128-byte value limit is remembered
	// by hash, with its full spelling still coming from the current card.
	char name[206];
	memset(name, 'a', 200);
	memcpy(name + 200, ".epub", 6);
	snprintf(longbook, sizeof(longbook), "%s/%s", folder, name);
	create_file(longbook);
	ebookhistory_remember(longbook);
	assert(config_store_init(store, config));
	assert(!ebookhistory_is_last(book));
	assert(ebookhistory_find_last(folder, out, sizeof(out)) && strcmp(out, longbook) == 0);
	assert(!ebookhistory_find_last(folder, out, 8) && out[0] == '\0');
	assert(!ebookhistory_find_last(folder, NULL, 0));
	assert(!ebookhistory_find_last(NULL, out, sizeof(out)) && out[0] == '\0');
	assert(!ebookhistory_find_last("/does-not-exist-sonix-books", out, sizeof(out)));
	assert(!ebookhistory_find_last(other, out, sizeof(out)));
	assert(unlink(longbook) == 0);
	assert(!ebookhistory_find_last(folder, out, sizeof(out)) && out[0] == '\0');

	// A folder named like a book and a remembered non-EPUB are never launched.
	snprintf(directory, sizeof(directory), "%s/Folder.epub", folder);
	assert(mkdir(directory, 0700) == 0);
	ebookhistory_remember(directory);
	assert(!ebookhistory_find_last(folder, out, sizeof(out)));
	char text[800];
	snprintf(text, sizeof(text), "%s/notes.txt", folder);
	create_file(text);
	ebookhistory_remember(text);
	assert(!ebookhistory_find_last(folder, out, sizeof(out)));

	assert(unlink(text) == 0);
	assert(rmdir(directory) == 0);
	assert(unlink(book) == 0);
	assert(unlink(config) == 0);
	assert(rmdir(folder) == 0);
	assert(rmdir(other) == 0);
	assert(rmdir(temp) == 0);
	puts("ebook history: all checks passed");
	return 0;
}
