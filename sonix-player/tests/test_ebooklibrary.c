#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0700)
#endif

#include "src/system/ebook/ebook.h"
#include "src/system/ebook/ebooklibrary.h"

/* The scanner's parser boundary: fake EPUBs deliberately have metadata unlike
 * their filenames. The existing EPUB engine is independently exercised by
 * tools/test_ebook.c; these tests isolate traversal/cache/search behavior. */
struct ebook { char title[256], author[128]; };
static unsigned opens;
ebook_t *ebook_open(const char *path) {
	opens++;
	FILE *f = fopen(path, "r");
	if (!f) return NULL;
	ebook_t *b = calloc(1, sizeof(*b));
	assert(b);
	if (!fgets(b->title, sizeof(b->title), f) || !strncmp(b->title, "BROKEN", 6)) {
		fclose(f); free(b); return NULL;
	}
	(void)fgets(b->author, sizeof(b->author), f);
	b->title[strcspn(b->title, "\r\n")] = 0;
	b->author[strcspn(b->author, "\r\n")] = 0;
	fclose(f);
	return b;
}
const char *ebook_title(const ebook_t *b) { return b->title; }
const char *ebook_author(const ebook_t *b) { return b->author; }
void ebook_close(ebook_t *b) { free(b); }

static void join(char *out, const char *dir, const char *name) {
	int n = snprintf(out, EBOOKLIBRARY_PATH, "%s/%s", dir, name);
	assert(n > 0 && n < EBOOKLIBRARY_PATH);
}
static void folder(const char *dir, const char *name) {
	char p[EBOOKLIBRARY_PATH]; join(p, dir, name); assert(MKDIR(p) == 0);
}
static void book(const char *dir, const char *file, const char *title, const char *author, long modified) {
	char p[EBOOKLIBRARY_PATH]; join(p, dir, file);
	FILE *f = fopen(p, "w"); assert(f);
	fprintf(f, "%s\n%s\n", title, author); assert(fclose(f) == 0);
	struct utimbuf time = {modified, modified}; assert(utime(p, &time) == 0);
}
static int index_of(ebooklibrary_result_t *r, const char *title) {
	for (int i = 0; i < r->count; i++) if (!strcmp(r->entries[i].title, title)) return i;
	return -1;
}
static bool cancel_now(void *user) { (void)user; return true; }
static bool match_path(const char *path, void *user) { return strstr(path, user) != NULL; }

int main(int argc, char **argv) {
	assert(argc == 2); /* caller supplies a fresh temporary folder */
	char root[EBOOKLIBRARY_PATH], cache[EBOOKLIBRARY_PATH], sub[EBOOKLIBRARY_PATH];
	join(root, argv[1], "Ebook"); assert(MKDIR(root) == 0);
	join(cache, argv[1], "cache"); assert(MKDIR(cache) == 0);
	folder(root, "Novels"); join(sub, root, "Novels");
	folder(sub, "Series");
	folder(root, "Empty.epub"); /* a directory ending in .epub remains a folder */
	book(root, "z-file.epub", "Alpha Story", "Zoe Writer", 1000000000);
	book(root, "a-file.EPUB", "Zulu Story", "Amy Writer", 1000000200);
	book(root, "bad.epub", "BROKEN", "", 1000000100);
	book(root, "ignored.txt", "Not yet supported", "", 1000000000);
	book(root, ".hidden.epub", "Hidden", "", 1000000000);
	book(sub, "buried.epub", "A Distant Book", "\xc3\x89mile Writer", 1000000000);
	book(sub, "symbols.epub", "100%_Literal", "Tester", 1000000000);
	char deeper[EBOOKLIBRARY_PATH]; join(deeper, sub, "Series");
	book(deeper, "third.epub", "\xce\x91\xce\x92\xce\x93", "\xd0\x90\xd0\x91\xd0\x92", 1000000000);
	ebooklibrary_result_t *r = malloc(sizeof(*r)); assert(r);
	ebooklibrary_scan(root, "", "", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 5 && !r->truncated && !r->unavailable);
	assert(r->entries[0].directory && r->entries[1].directory);
	assert(!strcmp(r->entries[2].title, "Alpha Story"));
	assert(!strcmp(r->entries[2].author, "Zoe Writer"));
	assert(index_of(r, "bad") >= 0 && index_of(r, "Hidden") < 0);
	unsigned first = opens;
	ebooklibrary_scan(root, "", "", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(opens == first + 1); /* only failed book is retried */

	ebooklibrary_scan(root, "", "", EBOOKLIBRARY_TITLE_DESC, cache, r, NULL, NULL);
	assert(!strcmp(r->entries[2].title, "Zulu Story"));
	ebooklibrary_scan(root, "", "", EBOOKLIBRARY_AUTHOR, cache, r, NULL, NULL);
	assert(!strcmp(r->entries[2].author, "Amy Writer"));
	assert(!strcmp(r->entries[4].title, "bad")); /* unknown author last */
	ebooklibrary_scan(root, "", "", EBOOKLIBRARY_NEWEST, cache, r, NULL, NULL);
	assert(!strcmp(r->entries[2].title, "Zulu Story"));
	ebooklibrary_scan(root, "", "", EBOOKLIBRARY_OLDEST, cache, r, NULL, NULL);
	assert(!strcmp(r->entries[2].title, "Alpha Story"));

	/* Search ignores the browsed folder, matching metadata throughout Ebook. */
	ebooklibrary_scan(root, "Novels/Series", "zoE wRitER", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1 && !strcmp(r->entries[0].title, "Alpha Story"));
	ebooklibrary_scan(root, "", "\xc3\xa9MILE", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1 && strstr(r->entries[0].path, "/Novels/buried.epub"));
	ebooklibrary_scan(root, "", "buried", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1); /* filename remains searchable after metadata loaded */
	ebooklibrary_scan(root, "", "%_", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1 && !strcmp(r->entries[0].title, "100%_Literal"));
	ebooklibrary_scan(root, "", "\xce\xb1\xce\xb2", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1); /* Greek */
	ebooklibrary_scan(root, "", "\xd0\xb0\xd0\xb1", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1); /* Cyrillic */
	ebooklibrary_scan(root, "Novels", "", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 3 && r->entries[0].directory);
	ebooklibrary_scan(root, "Novels/Series", "", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1 && !r->entries[0].directory);
	ebooklibrary_scan(root, "Empty.epub", "", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 0 && !r->unavailable);

	/* Updated files invalidate metadata; absent/corrupt caches are optional. */
	book(root, "z-file.epub", "Changed title is longer", "Different Writer", 1000000300);
	ebooklibrary_scan(root, "", "Different Writer", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1 && !strcmp(r->entries[0].title, "Changed title is longer"));
	DIR *dir = opendir(cache); assert(dir); struct dirent *de;
	while ((de = readdir(dir))) if (de->d_name[0] != '.') {
		char p[EBOOKLIBRARY_PATH]; join(p, cache, de->d_name);
		FILE *f = fopen(p, "w"); assert(f); fputs("corrupt", f); fclose(f);
	}
	closedir(dir);
	ebooklibrary_scan(root, "", "Different Writer", EBOOKLIBRARY_TITLE, cache, r, NULL, NULL);
	assert(r->count == 1);
	ebooklibrary_scan(root, "", "Different Writer", EBOOKLIBRARY_TITLE, "/missing/cache", r, NULL, NULL);
	assert(r->count == 1);

	const char *invalid[] = {"../", "Novels/../", "/Novels", "Novels//Series", "Novels/", "Novels\\Series", "C:foo", "missing"};
	for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		ebooklibrary_scan(root, invalid[i], "", EBOOKLIBRARY_TITLE, NULL, r, NULL, NULL);
		assert(r->unavailable && !r->count);
	}
	first = opens;
	ebooklibrary_scan(root, "", "Writer", EBOOKLIBRARY_TITLE, NULL, r, cancel_now, NULL);
	assert(r->cancelled && !r->count && opens == first);
	char found[EBOOKLIBRARY_PATH];
	assert(ebooklibrary_find(root, match_path, "third.epub", found, sizeof(found), NULL));
	assert(strstr(found, "/Novels/Series/third.epub") && opens == first);
	assert(!ebooklibrary_find(root, match_path, "third.epub", found, 4, NULL) && !found[0]);
	assert(!ebooklibrary_find(root, match_path, "third.epub", found, sizeof(found), cancel_now));

#ifndef _WIN32
	/* Neither global search nor directly entering a symlink may leave Ebook. */
	char outside[EBOOKLIBRARY_PATH], link[EBOOKLIBRARY_PATH];
	join(outside, argv[1], "outside"); assert(MKDIR(outside) == 0);
	book(outside, "secret.epub", "Outside Secret", "Outside Author", 1000000000);
	join(link, root, "escape"); assert(symlink(outside, link) == 0);
	ebooklibrary_scan(root, "", "Outside Author", EBOOKLIBRARY_TITLE, NULL, r, NULL, NULL);
	assert(!r->count);
	ebooklibrary_scan(root, "escape", "", EBOOKLIBRARY_TITLE, NULL, r, NULL, NULL);
	assert(r->unavailable);
#endif

	/* Oversized directories keep the sorted first 300, with an honest limit. */
	folder(root, "Many"); char many[EBOOKLIBRARY_PATH]; join(many, root, "Many");
	for (int i = EBOOKLIBRARY_MAX + 4; i >= 0; i--) {
		char file[32], title[32];
		snprintf(file, sizeof(file), "book%04d.epub", i);
		snprintf(title, sizeof(title), "Title %04d", i);
		book(many, file, title, "Crowd Author", 1000000000);
	}
	ebooklibrary_scan(root, "Many", "", EBOOKLIBRARY_TITLE, NULL, r, NULL, NULL);
	assert(r->truncated && r->count == EBOOKLIBRARY_MAX);
	assert(!strcmp(r->entries[0].title, "Title 0000") && !strcmp(r->entries[299].title, "Title 0299"));
	ebooklibrary_scan(root, "Many", "", EBOOKLIBRARY_TITLE_DESC, NULL, r, NULL, NULL);
	assert(r->truncated && !strcmp(r->entries[0].title, "Title 0304"));

	/* Excessive nesting is bounded and reported, rather than recursing forever. */
	folder(root, "Deep"); char deep[EBOOKLIBRARY_PATH]; join(deep, root, "Deep");
	for (int i = 0; i < EBOOKLIBRARY_DEPTH + 1; i++) {
		folder(deep, "d"); size_t n = strlen(deep); memcpy(deep + n, "/d", 3);
	}
	book(deep, "deep.epub", "Too deep", "Deep Author", 1000000000);
	ebooklibrary_scan(root, "", "Deep Author", EBOOKLIBRARY_TITLE, NULL, r, NULL, NULL);
	assert(r->truncated && !r->count);
	free(r);
	puts("ebook library: metadata, search, folders, five sorts, cache, limits and cancellation passed");
	return 0;
}
