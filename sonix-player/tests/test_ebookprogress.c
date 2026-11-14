#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/core/config.h"
#include "src/system/ebook/bookmarks.h"
#include "src/system/ebook/ebookprogress.h"

static void key_for(const char *path, char *key, size_t size) {
	char hash[24]; bookmark_key(path, hash, sizeof(hash));
	snprintf(key, size, "pos_%s", hash);
}

static void reload(config_store_t *store) {
	char path[512];
	snprintf(path, sizeof(path), "%s", config_store_path(store));
#ifdef _WIN32
	/* The deployed Linux rename replaces an existing destination. Native
	 * Windows CRT rename does not; remove the test file before this flush. */
	remove(path);
#endif
	assert(config_store_save(store));
	assert(config_store_init(store, path));
}

static void position_is(ebook_position_t p, uint32_t spine, uint32_t block, uint32_t offset) {
	assert(p.spine == spine && p.block == block && p.offset == offset);
}

int main(int argc, char **argv) {
	assert(argc == 2); /* caller supplies a fresh temporary folder */
	char file[512];
	assert(snprintf(file, sizeof(file), "%s/ebook.ini", argv[1]) < (int)sizeof(file));
	config_store_t *store = config_ebook_store();
	(void)config_store_init(store, file);
	config_store_set_int(store, "ebook", "font_size", 26);
	const char *book = "/card/Ebook/Novels/Book.epub";
	const char *other = "/card/Ebook/Other/Book.epub";
	char key[32]; key_for(book, key, sizeof(key));

	ebookprogress_t p = ebookprogress_get(book);
	assert(p.state == EBOOKPROGRESS_NEW && p.known && p.percent == 0);
	position_is(p.position, 0, 0, 0);
	assert(ebookprogress_get(NULL).state == EBOOKPROGRESS_NEW);
	assert(ebookprogress_get("").state == EBOOKPROGRESS_NEW);

	/* Legacy positions keep their exact resume point, without inventing a %.
	 * Reading the status does not rewrite that entry. */
	config_store_set(store, "ebook", key, "3:14:8");
	p = ebookprogress_get(book);
	assert(p.state == EBOOKPROGRESS_READING && !p.known);
	position_is(p.position, 3, 14, 8);
	assert(!strcmp(config_store_get(store, "ebook", key, ""), "3:14:8"));
	assert(ebookprogress_get(other).state == EBOOKPROGRESS_NEW);

	/* Opening a book at its start becomes Reading 0%, distinct from New. */
	ebookprogress_t fresh = ebookprogress_advance(ebookprogress_get(other), (ebook_position_t){0,0,0}, 0, false);
	assert(fresh.state == EBOOKPROGRESS_READING && fresh.percent == 0 && fresh.known);
	ebookprogress_store(other, &fresh);
	p = ebookprogress_advance(p, (ebook_position_t){3,20,17}, 37, false);
	ebookprogress_store(book, &p);
	reload(store);
	p = ebookprogress_get(book);
	assert(p.state == EBOOKPROGRESS_READING && p.percent == 37 && p.known);
	position_is(p.position, 3, 20, 17);
	assert(ebookprogress_get(other).state == EBOOKPROGRESS_READING);
	assert(config_store_get_int(store, "ebook", "font_size", 0) == 26);
	unsigned a, b, c;
	assert(sscanf(config_store_get(store, "ebook", key, ""), "%u:%u:%u", &a, &b, &c) == 3);
	assert(a == 3 && b == 20 && c == 17); /* an old reader can still resume */

	/* Estimates never mark a book finished; only the end action does. */
	ebookprogress_t estimated = ebookprogress_advance(p, (ebook_position_t){9,10,0}, 100, false);
	assert(estimated.state == EBOOKPROGRESS_READING && estimated.percent == 99);
	p = ebookprogress_advance(p, (ebook_position_t){9,10,0}, 96, true);
	assert(p.state == EBOOKPROGRESS_FINISHED && p.percent == 100);
	ebookprogress_store(book, &p);
	reload(store);
	p = ebookprogress_get(book);
	assert(p.state == EBOOKPROGRESS_FINISHED && p.percent == 100 && p.known);
	p = ebookprogress_advance(p, p.position, 96, false);
	assert(p.state == EBOOKPROGRESS_FINISHED); /* reopening, repainting, or closing */
	p = ebookprogress_advance(p, (ebook_position_t){9,10,12}, 97, false);
	assert(p.state == EBOOKPROGRESS_FINISHED);
	p = ebookprogress_advance(p, (ebook_position_t){9,10,0}, 96, false);
	assert(p.state == EBOOKPROGRESS_READING && p.percent == 96); /* rewind */
	ebookprogress_store(book, &p);
	reload(store);
	assert(ebookprogress_get(book).state == EBOOKPROGRESS_READING);
	p = ebookprogress_advance(p, (ebook_position_t){9,10,12}, 97, true);
	assert(ebookprogress_advance(p, (ebook_position_t){9,9,999}, 95, false).state == EBOOKPROGRESS_READING);
	assert(ebookprogress_advance(p, (ebook_position_t){8,999,999}, 90, false).state == EBOOKPROGRESS_READING);
	assert(ebookprogress_advance(p, (ebook_position_t){9,10,12}, INT_MAX, false).percent == 100);
	p = ebookprogress_advance((ebookprogress_t){0}, (ebook_position_t){0,0,0}, -10, false);
	assert(p.percent == 0 && p.state == EBOOKPROGRESS_READING);
	assert(ebookprogress_advance(p, p.position, INT_MAX, false).percent == 99);

	/* Corrupt/newer extensions don't destroy a usable three-number resume. */
	const char *bad[] = {"3:14:8|p1:100:0", "3:14:8|p1:99:1", "3:14:8|p1:101:1", "3:14:8|p1:9:2",
		"3:14:8|p1:-1:0", "3:14:8|p1:9999999999999999999999999999999:0", "3:14:8|p1:99:1junk", "3:14:8|p2:9:0", "3:14:8|p1:"};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
		config_store_set(store, "ebook", key, bad[i]);
		p = ebookprogress_get(book);
		assert(p.state == EBOOKPROGRESS_READING && !p.known);
		position_is(p.position, 3, 14, 8);
		assert(!strcmp(config_store_get(store, "ebook", key, ""), bad[i]));
	}
	const char *bad_position[] = {"", "3:14", "3::8", "-1:14:8", "4294967296:14:8", "invalid"};
	for (unsigned i = 0; i < sizeof(bad_position) / sizeof(*bad_position); i++) {
		config_store_set(store, "ebook", key, bad_position[i]);
		p = ebookprogress_get(book);
		assert(p.state == EBOOKPROGRESS_NEW);
		position_is(p.position, 0, 0, 0);
	}
	p = ebookprogress_advance(p, (ebook_position_t){UINT32_MAX,UINT32_MAX,UINT32_MAX}, 50, false);
	ebookprogress_store(book, &p);
	position_is(ebookprogress_get(book).position, UINT32_MAX, UINT32_MAX, UINT32_MAX);
	assert(strlen(config_store_get(store, "ebook", key, "")) < 64);
	ebookprogress_store(book, NULL);
	ebookprogress_store(NULL, &p);
	ebookprogress_store(book, &(ebookprogress_t){.state = EBOOKPROGRESS_NEW});
	assert(ebookprogress_get(book).percent == 50);

	/* Full path hashes keep long/nested paths separate, including same names. */
	char long_path[700]; memset(long_path, 'x', sizeof(long_path));
	memcpy(long_path, "/card/Ebook/", 12);
	memcpy(long_path + sizeof(long_path) - 6, ".epub", 6);
	ebookprogress_store(long_path, &p);
	assert(ebookprogress_get(long_path).percent == 50);
	assert(ebookprogress_get(other).percent == 0);

	/* Progress advances within a long paragraph, without scanning a chapter. */
	assert(ebookprogress_chapter_percent(0, 1, 50, 100) == 50);
	assert(ebookprogress_chapter_percent(5, 10, 50, 100) == 55);
	assert(ebookprogress_chapter_percent(5, 10, 999, 100) == 60);
	assert(ebookprogress_chapter_percent(5, 10, 0, 0) == 50);
	assert(ebookprogress_chapter_percent(0, 0, 0, 0) == 0);
	assert(ebookprogress_chapter_percent(10, 10, 0, 0) == 100);
	assert(ebookprogress_chapter_percent(UINT32_MAX - 1, UINT32_MAX, UINT32_MAX, UINT32_MAX) == 100);
	assert(ebookprogress_chapter_percent(0, 1, UINT32_MAX, UINT64_MAX) == 0);
	puts("ebook progress: states, persistence, legacy resume, rewind, invalid data and paragraph offsets passed");
	return 0;
}
