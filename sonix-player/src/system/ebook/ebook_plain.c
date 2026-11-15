#define _POSIX_C_SOURCE 200809L
#include "ebook_native.h"
#include "ebooktext.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEXT_CHUNK 65536u
static const uint16_t cp1252[32] = {
	0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
	0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
};
static unsigned unit(const unsigned char *p, unsigned encoding) {
	return encoding == 1 ? (unsigned)p[0] | (unsigned)p[1] << 8 : (unsigned)p[0] << 8 | p[1];
}
bool ebook_text_open(ebook_t *book) {
	book->text_file = fopen(book->path, "rb");
	if (!book->text_file) return false;
	struct stat st;
	if (fstat(fileno(book->text_file), &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > BOOKTOOL_FILE_MAX) return false;
	unsigned char bom[4] = {0}; size_t n = fread(bom, 1, sizeof(bom), book->text_file);
	if (n == 4 && ((!memcmp(bom, "\xff\xfe\0\0", 4)) || !memcmp(bom, "\0\0\xfe\xff", 4))) {
		ebook_native_error("Save this text file as UTF-8 or UTF-16 before opening it."); return false;
	}
	if (n >= 3 && !memcmp(bom, "\xef\xbb\xbf", 3)) book->text_start = 3;
	else if (n >= 2 && ((!memcmp(bom, "\xff\xfe", 2)) || !memcmp(bom, "\xfe\xff", 2))) {
		book->text_start = 2; book->text_encoding = bom[0] == 0xff ? 1 : 2;
	}
	book->text_size = (uint32_t)st.st_size - book->text_start;
	if (book->text_encoding && (book->text_size & 1)) return false;
	book->spine_count = book->text_size ? (book->text_size + TEXT_CHUNK - 1) / TEXT_CHUNK : 1;
	book->text_offsets = arena_alloc(&book->book_arena, (book->spine_count + 1) * sizeof(uint32_t));
	book->weights = arena_alloc(&book->book_arena, book->spine_count * sizeof(uint32_t));
	if (!book->text_offsets || !book->weights) return false;
	book->text_offsets[0] = 0;
	for (uint32_t i = 1; i < book->spine_count; i++) {
		uint32_t boundary = i * TEXT_CHUNK;
		if (fseek(book->text_file, book->text_start + boundary, SEEK_SET)) return false;
		unsigned char bytes[4]; size_t got = fread(bytes, 1, sizeof(bytes), book->text_file);
		if (book->text_encoding) {
			if (got >= 2 && unit(bytes, book->text_encoding) >= 0xdc00 && unit(bytes, book->text_encoding) <= 0xdfff) boundary += 2;
		} else {
			unsigned extra = 0;
			while (extra < got && extra < 3 && (bytes[extra] & 0xc0) == 0x80) extra++;
			boundary += extra;
		}
		book->text_offsets[i] = boundary;
	}
	book->text_offsets[book->spine_count] = book->text_size;
	for (uint32_t i = 0; i < book->spine_count; i++) book->weights[i] = book->text_offsets[i + 1] - book->text_offsets[i];
	book->weights_total = book->text_size;
	return true;
}
static char *put_rune(char *out, uint32_t c) {
	if (c == '&') { memcpy(out, "&amp;", 5); return out + 5; }
	if (c == '<') { memcpy(out, "&lt;", 4); return out + 4; }
	if (c == '>') { memcpy(out, "&gt;", 4); return out + 4; }
	if (!c || c == 127 || (c < 32 && c != '\t')) c = 0xfffd;
	if (c < 128) *out++ = (char)c;
	else if (c < 0x800) { *out++ = (char)(0xc0 | c >> 6); *out++ = (char)(0x80 | (c & 63)); }
	else if (c < 0x10000) { *out++ = (char)(0xe0 | c >> 12); *out++ = (char)(0x80 | ((c >> 6) & 63)); *out++ = (char)(0x80 | (c & 63)); }
	else { *out++ = (char)(0xf0 | c >> 18); *out++ = (char)(0x80 | ((c >> 12) & 63)); *out++ = (char)(0x80 | ((c >> 6) & 63)); *out++ = (char)(0x80 | (c & 63)); }
	return out;
}
bool ebook_text_chapter(ebook_t *book, uint32_t chapter) {
	uint32_t start = book->text_offsets[chapter], size = book->text_offsets[chapter + 1] - start;
	unsigned char *bytes = arena_alloc(&book->chapter_arena, (size_t)size + 1);
	char *html = arena_alloc(&book->chapter_arena, (size_t)size * 8 + 64);
	if (!bytes || !html || fseek(book->text_file, book->text_start + start, SEEK_SET) || fread(bytes, 1, size, book->text_file) != size) return false;
	char *out = html; memcpy(out, "<html><body><p>", 15); out += 15;
	bool cr = false;
	for (size_t at = 0; at < size;) {
		uint32_t c;
		if (book->text_encoding) {
			c = unit(bytes + at, book->text_encoding); at += 2;
			if (c >= 0xd800 && c <= 0xdbff && at + 2 <= size) {
				unsigned low = unit(bytes + at, book->text_encoding);
				if (low >= 0xdc00 && low <= 0xdfff) { c = 0x10000 + (c - 0xd800) * 1024 + low - 0xdc00; at += 2; }
			}
			if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
		} else {
			size_t before = at; c = ebooktext_decode((const char *)bytes, size, &at);
			if (c == 0xfffd && at == before + 1) { unsigned b = bytes[before]; c = b >= 128 && b < 160 ? cp1252[b - 128] : b; }
		}
		if (c == '\r' || c == '\n') {
			if (!(cr && c == '\n')) { memcpy(out, "</p><p>", 7); out += 7; }
			cr = c == '\r'; continue;
		}
		cr = false; out = put_rune(out, c);
	}
	memcpy(out, "</p></body></html>", 19);
	return epub_parse_chapter(book, html);
}
