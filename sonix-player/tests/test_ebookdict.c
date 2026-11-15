#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "src/system/ebook/ebookdict.h"

static char root[600], folder[800], bad[900];
static ebookdict_result_t result;
static unsigned calls, stop_after;

static bool cancel(void *unused) { (void)unused; return ++calls > stop_after; }
static void put32(FILE *file, uint32_t n) {
	unsigned char bytes[4] = {(unsigned char)n, (unsigned char)(n >> 8), (unsigned char)(n >> 16), (unsigned char)(n >> 24)};
	assert(fwrite(bytes, 1, 4, file) == 4);
}
static void fixture(const char *path, const char *text, size_t length) {
	FILE *f = fopen(path, "wb"); assert(f);
	assert(fwrite("SNXDICT1", 1, 8, f) == 8); put32(f, 1); put32(f, 104);
	char word[96] = "apple"; assert(fwrite(word, 1, 96, f) == 96);
	put32(f, 120); put32(f, (uint32_t)length);
	assert(fwrite(text, 1, length, f) == length); assert(!fclose(f));
}
static void patch(uint32_t at, uint32_t value) {
	FILE *f = fopen(bad, "r+b"); assert(f); assert(!fseek(f, at, SEEK_SET)); put32(f, value); assert(!fclose(f));
}
static void lookup(const char *word, ebookdict_status_t status) {
	ebookdict_lookup(root, "Fixture.sxd", word, &result, NULL, NULL);
	assert(result.status == status && result.probes <= 20);
	if (status != EBOOKDICT_OK) assert(!result.text[0]);
}
static void invalid(void) {
	ebookdict_lookup(root, "Bad.sxd", "apple", &result, NULL, NULL);
	assert(result.status == EBOOKDICT_BAD_FILE && !result.text[0]);
}

int main(int argc, char **argv) {
	assert(argc == 2 && strlen(argv[1]) < sizeof(root)); strcpy(root, argv[1]);
	snprintf(folder, sizeof(folder), "%s/Ebook/Dictionaries", root);
	snprintf(bad, sizeof(bad), "%s/Bad.sxd", folder);
	lookup("  \"APPLE,\"  ", EBOOKDICT_OK); assert(!strcmp(result.word, "apple") && !strcmp(result.text, "A fruit.\nA second sense."));
	lookup("CAFÉ", EBOOKDICT_OK); assert(!strcmp(result.text, "Coffee house."));
	lookup("DON’T", EBOOKDICT_OK); assert(!strcmp(result.word, "don't"));
	lookup(" north__  america ", EBOOKDICT_OK); assert(!strcmp(result.word, "north america"));
	lookup("ΟΣ", EBOOKDICT_OK); lookup("ος", EBOOKDICT_OK); lookup("КОТ", EBOOKDICT_OK); lookup("世界", EBOOKDICT_OK);
	lookup("unlisted", EBOOKDICT_NOT_FOUND); lookup("STRASSE", EBOOKDICT_NOT_FOUND);
	lookup("...", EBOOKDICT_EMPTY); lookup("", EBOOKDICT_EMPTY); lookup("bad\1", EBOOKDICT_BAD_QUERY);
	lookup("\xc0\xaf", EBOOKDICT_BAD_QUERY);
	char large[300]; memset(large, 'q', 95); large[95] = 0; lookup(large, EBOOKDICT_OK);
	large[95] = 'q'; large[96] = 0; lookup(large, EBOOKDICT_LONG_QUERY);
	memset(large, 'q', sizeof(large) - 1); large[sizeof(large) - 1] = 0; lookup(large, EBOOKDICT_LONG_QUERY);
	lookup("long", EBOOKDICT_OK); assert(result.truncated && strlen(result.text) == 8191);
	for (unsigned i = 0; i < 10000; i++) {
		char word[32], expected[32]; snprintf(word, sizeof(word), "entry%05u", i); snprintf(expected, sizeof(expected), "Definition %u", i);
		lookup(word, EBOOKDICT_OK); assert(!strcmp(result.text, expected));
	}
	// All calls close their file. Cancellation can happen during the binary search.
	calls = 0; stop_after = 3;
	ebookdict_lookup(root, "Fixture.sxd", "entry09000", &result, cancel, NULL);
	assert(result.status == EBOOKDICT_CANCELLED && !result.text[0]);
	const char *bad_ids[] = {"../Fixture.sxd", "/Fixture.sxd", "A/Fixture.sxd", "A\\Fixture.sxd", "C:Fixture.sxd", ".hidden.sxd", "bad.txt"};
	for (unsigned i = 0; i < sizeof(bad_ids) / sizeof(*bad_ids); i++) {
		ebookdict_lookup(root, bad_ids[i], "apple", &result, NULL, NULL); assert(result.status == EBOOKDICT_UNAVAILABLE);
	}
	// Header and record validation: no offsets can escape the data region/file.
	const uint32_t corrupt[][2] = {{0, 0}, {8, 0}, {8, 500001}, {12, 103}, {112, 0}, {112, UINT32_MAX}, {116, 0}, {116, 65537}, {116, UINT32_MAX}};
	for (unsigned i = 0; i < sizeof(corrupt) / sizeof(*corrupt); i++) {
		fixture(bad, "definition", 10); patch(corrupt[i][0], corrupt[i][1]); invalid();
	}
	fixture(bad, "definition", 10);
	FILE *f = fopen(bad, "r+b"); assert(f); assert(!fseek(f, 16, SEEK_SET));
	for (unsigned i = 0; i < 96; i++) fputc('a', f);
	assert(!fclose(f)); invalid();
	fixture(bad, "definition", 10); patch(16, 0x4c505041u); invalid(); // noncanonical APPLe
	fixture(bad, "\xff", 1); invalid(); fixture(bad, "a\0b", 3); invalid();
	f = fopen(bad, "wb"); assert(f); fputs("truncated", f); assert(!fclose(f)); invalid();
	assert(!unlink(bad));
	// Links, special files, missing roots and card removal never become reads.
	char link[900]; snprintf(link, sizeof(link), "%s/Link.sxd", folder);
	assert(!symlink("Fixture.sxd", link));
	ebookdict_lookup(root, "Link.sxd", "apple", &result, NULL, NULL); assert(result.status == EBOOKDICT_UNAVAILABLE);
	assert(!unlink(link)); assert(!mkfifo(link, 0600));
	ebookdict_lookup(root, "Link.sxd", "apple", &result, NULL, NULL); assert(result.status == EBOOKDICT_UNAVAILABLE); assert(!unlink(link));
	ebookdict_catalog_t catalog; ebookdict_scan(root, &catalog, NULL, NULL);
	assert(catalog.count == 1 && !catalog.unavailable && !strcmp(catalog.entries[0].id, "Fixture.sxd"));
	char moved[850]; snprintf(moved, sizeof(moved), "%s.off", folder); assert(!rename(folder, moved));
	ebookdict_scan(root, &catalog, NULL, NULL); assert(catalog.unavailable && !catalog.count);
	lookup("apple", EBOOKDICT_UNAVAILABLE); assert(!rename(moved, folder));
	for (unsigned i = 0; i < 12; i++) {
		char file[900]; snprintf(file, sizeof(file), "%s/D%02u.sxd", folder, i); fixture(file, "text", 4);
	}
	ebookdict_scan(root, &catalog, NULL, NULL); assert(catalog.count == 8 && catalog.truncated && !strcmp(catalog.entries[0].name, "D00") && !strcmp(catalog.entries[7].name, "D07"));
	calls = 0; stop_after = 3; ebookdict_scan(root, &catalog, cancel, NULL); assert(catalog.cancelled);
	for (unsigned i = 0; i < 80; i++) {
		char file[900]; snprintf(file, sizeof(file), "%s/Junk%02u.txt", folder, i); f = fopen(file, "w"); assert(f); assert(!fclose(f));
	}
	ebookdict_scan(root, &catalog, NULL, NULL); assert(catalog.truncated && catalog.count <= 8);
	puts("dictionary: converter interoperability, 10,000 indexed lookups, Unicode, bounds, clipping, cancellation, missing card and path checks passed");
	return 0;
}
