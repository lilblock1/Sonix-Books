#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/system/ebook/bookmarks.h"
#include "src/system/core/config.h"

static char config[512];
static const char *book = "/card/Ebook/folder/Book.epub";
static config_store_t *store;

static void reload(void) { assert(config_store_init(store, config)); }
static void key(char *out, size_t size, char kind, int index) {
	char hash[17]; bookmark_key(book, hash, sizeof(hash));
	snprintf(out, size, "%c_%s_%d", kind, hash, index);
}
static void assert_mark(int index, const bookmark_t *expected) {
	bookmark_t got; assert(bookmark_get(book, index, &got));
	assert(got.spine == expected->spine && got.block == expected->block && got.offset == expected->offset);
	assert(!strcmp(got.text, expected->text));
	assert(!strcmp(got.name, expected->name));
	assert(!strcmp(got.note, expected->note));
}

int main(void) {
	char temp[] = "/tmp/sonix-notes-XXXXXX"; assert(mkdtemp(temp));
	snprintf(config, sizeof(config), "%s/ebook.ini", temp);
	store = config_ebook_store(); (void)config_store_init(store, config);
	config_store_set(store, "ebook", "pos_existing", "3:14:8|p1:37:0");
	config_store_set(store, "ebook", "font_file", "OpenDyslexic/Regular.otf");
	char countkey[32], entrykey[32];
	char hash[17]; bookmark_key(book, hash, sizeof(hash));
	snprintf(countkey, sizeof(countkey), "n_%s", hash); key(entrykey, sizeof(entrykey), 'b', 0);
	// Legacy marks still open; no eager migration or changes to reading state.
	config_store_set(store, "bookmarks", countkey, "1");
	config_store_set(store, "bookmarks", entrykey, "3:41:120|An old excerpt: with a colon");
	assert(config_store_save(store)); reload();
	bookmark_t old; assert(bookmark_get(book, 0, &old));
	assert(old.spine == 3 && old.block == 41 && old.offset == 120 && !old.name[0] && !old.note[0]);
	assert(!strcmp(old.text, "An old excerpt: with a colon"));
	assert(bookmark_save(book, -1, &old) == BOOKMARK_DUPLICATE);
	// Separators, whitespace, line breaks and INI-looking text round-trip exactly.
	strcpy(old.name, "  Café | 世界 = [name]  ");
	strcpy(old.note, "First line\n[ebook]\nfont_size = 99\nSecond: | = ; #\t🙂  ");
	assert(bookmark_save(book, 0, &old) == BOOKMARK_OK); reload(); assert_mark(0, &old);
	assert(!strcmp(config_store_get(store, "ebook", "pos_existing", ""), "3:14:8|p1:37:0"));
	assert(!strcmp(config_store_get(store, "ebook", "font_file", ""), "OpenDyslexic/Regular.otf"));
	assert(!strcmp(config_store_get(store, "ebook", "font_size", "missing"), "missing"));
	// Corrupt annotation data is ignored without losing a valid bookmark.
	key(entrykey, sizeof(entrykey), 't', 0);
	config_store_set(store, "bookmarks", entrykey, "not hex");
	bookmark_t corrupt; assert(bookmark_get(book, 0, &corrupt) && !corrupt.name[0] && corrupt.note[0]);
	assert(bookmark_save(book, 0, &old) == BOOKMARK_OK);
	config_change_t invalid_change = {"unsafe", "line\n[ebook]"};
	assert(config_store_apply(store, "bookmarks", &invalid_change, 1) == CONFIG_APPLY_INVALID);
	assert_mark(0, &old);
	// Position is immutable during edit; rejected edits leave the old value.
	bookmark_t bad = old; bad.spine++;
	assert(bookmark_save(book, 0, &bad) == BOOKMARK_INVALID); assert_mark(0, &old);
	bad = old; memset(bad.name, 'x', sizeof(bad.name));
	assert(bookmark_save(book, 0, &bad) == BOOKMARK_INVALID);
	bad = old; strcpy(bad.name, "bad\nname");
	assert(bookmark_save(book, 0, &bad) == BOOKMARK_INVALID);
	bad = old; memcpy(bad.note, "\xc0\xaf", 3);
	assert(bookmark_save(book, 0, &bad) == BOOKMARK_INVALID);
	// Exact byte limits, including UTF-8 across the note chunk boundary.
	bookmark_t second = {.spine = 7, .block = 9, .offset = 11};
	memset(second.name, 'N', sizeof(second.name) - 1);
	memset(second.note, 'a', sizeof(second.note) - 1);
	memcpy(second.note + 59, "世界", 6);
	strcpy(second.text, "Second excerpt");
	assert(bookmark_save(book, -1, &second) == BOOKMARK_OK); reload(); assert_mark(1, &second);
	assert(bookmark_find(book, 7, 9, 11) == 1);
	// Book identity includes the full nested path.
	assert(bookmark_add("/card/Ebook/other/Book.epub", 7, 9, 11, "Other book"));
	assert(bookmark_count(book) == 2 && bookmark_count("/card/Ebook/other/Book.epub") == 1);
	// A failed replacement must roll back metadata, counts and deletions.
	char blocked[560]; snprintf(blocked, sizeof(blocked), "%s.tmp", config);
	assert(mkdir(blocked, 0700) == 0);
	bad = old; strcpy(bad.note, "This must not be committed");
	assert(bookmark_save(book, 0, &bad) == BOOKMARK_SAVE_FAILED);
	assert_mark(0, &old); assert(!bookmark_remove(book, 0)); assert(bookmark_count(book) == 2);
	assert(rmdir(blocked) == 0); reload(); assert_mark(0, &old); assert_mark(1, &second);
	// Removing shifts the entire annotation and removes the obsolete tail keys.
	assert(bookmark_remove(book, 0)); reload(); assert_mark(0, &second);
	key(entrykey, sizeof(entrykey), 't', 1);
	assert(!strcmp(config_store_get(store, "bookmarks", entrykey, "gone"), "gone"));
	second.name[0] = second.note[0] = 0;
	assert(bookmark_save(book, 0, &second) == BOOKMARK_OK); reload(); assert_mark(0, &second);
	key(entrykey, sizeof(entrykey), 't', 0);
	assert(!strcmp(config_store_get(store, "bookmarks", entrykey, "gone"), "gone"));
	// Old firmware moving the legacy position cannot attach the wrong note.
	strcpy(second.name, "Belongs to chapter 7");
	assert(bookmark_save(book, 0, &second) == BOOKMARK_OK);
	key(entrykey, sizeof(entrykey), 'b', 0);
	config_store_set(store, "bookmarks", entrykey, "8:9:11|Moved by older firmware");
	bookmark_t got; assert(bookmark_get(book, 0, &got) && got.spine == 8 && !got.name[0] && !got.note[0]);
	// Malformed/overflowed positions are rejected rather than wrapping.
	const char *invalid[] = {"-1:2:3|bad", "4294967296:2:3|bad", "1:2:3junk", "1:2", "1::3"};
	for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		config_store_set(store, "bookmarks", entrykey, invalid[i]); assert(!bookmark_get(book, 0, &got));
	}
	// A completely annotated book fits the existing 512-entry store.
	remove(config); (void)config_store_init(store, config);
	for (int i = 0; i < BOOKMARK_MAX_PER_BOOK; i++) {
		bookmark_t mark = {.spine = (uint32_t)i, .block = 2, .offset = 3};
		memset(mark.name, 'n', sizeof(mark.name) - 1); memset(mark.note, 'z', sizeof(mark.note) - 1);
		assert(bookmark_save(book, -1, &mark) == BOOKMARK_OK);
	}
	reload(); assert(bookmark_count(book) == 64);
	bookmark_t extra = {.spine = 100}; assert(bookmark_save(book, -1, &extra) == BOOKMARK_FULL);
	assert(bookmark_get(book, 63, &got) && strlen(got.note) == BOOKMARK_NOTE_MAX - 1);
	// A full shared table fails cleanly before any partial key is written.
	for (int i = 0; i < 512; i++) {
		char filler[32]; snprintf(filler, sizeof(filler), "f%d", i);
		config_store_set(store, "filler", filler, "x");
		if (!strcmp(config_store_get(store, "filler", filler, "missing"), "missing")) break;
	}
	assert(config_store_save(store));
	assert(bookmark_save("/card/Ebook/new.epub", -1, &extra) == BOOKMARK_STORAGE_FULL);
	assert(!bookmark_count("/card/Ebook/new.epub")); reload(); assert(bookmark_count(book) == 64);
	assert(bookmark_remove(book, 30)); reload();
	assert(bookmark_count(book) == 63 && bookmark_get(book, 30, &got) && got.spine == 31 && got.name[0] && got.note[0]);
	// The longest valid position still leaves a bounded, complete UTF-8 snippet.
	remove(config); (void)config_store_init(store, config);
	bookmark_t largest = {.spine = UINT32_MAX, .block = UINT32_MAX, .offset = UINT32_MAX};
	memset(largest.text, 'x', sizeof(largest.text) - 1);
	assert(bookmark_save(book, -1, &largest) == BOOKMARK_OK); reload();
	assert(bookmark_get(book, 0, &got) && got.spine == UINT32_MAX && got.offset == UINT32_MAX);
	assert(strlen(got.text) == 94);
	puts("bookmark notes: legacy, Unicode, boundaries, isolation, deletion, full storage and failed-save rollback passed");
	remove(config); assert(rmdir(temp) == 0);
	return 0;
}
