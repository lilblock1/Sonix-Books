#define _POSIX_C_SOURCE 200809L
#include "ebookdict.h"
#include "ebooktext.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#define lstat stat
#endif

#define DICT_PATH 800
#define HEADER 16u
#define RECORD (EBOOKDICT_WORD + 8u)

static bool stopped(ebookdict_cancel_t cancel, void *user) { return cancel && cancel(user); }
static bool ordinary(const char *path, struct stat *st) {
#ifdef _WIN32
	DWORD attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
	return lstat(path, st) == 0;
}
static bool root_path(const char *root, char out[DICT_PATH]) {
	if (!root || !root[0]) return false;
	int n = snprintf(out, DICT_PATH, "%s/Ebook", root);
	struct stat st;
	if (n < 0 || n >= DICT_PATH || !ordinary(out, &st) || !S_ISDIR(st.st_mode)) return false;
	size_t used = (size_t)n;
	if (used + sizeof("/Dictionaries") > DICT_PATH) return false;
	memcpy(out + used, "/Dictionaries", sizeof("/Dictionaries"));
	return ordinary(out, &st) && S_ISDIR(st.st_mode);
}
static bool valid_id(const char *id) {
	if (!id || !id[0] || id[0] == '.') return false;
	size_t n = strlen(id);
	if (n <= 4 || n >= EBOOKDICT_ID || strcasecmp(id + n - 4, ".sxd")) return false;
	for (size_t i = 0; i < n; i++)
		if ((unsigned char)id[i] < 32 || id[i] == 127 || strchr("/\\:", id[i])) return false;
	return true;
}

void ebookdict_scan(const char *root, ebookdict_catalog_t *out, ebookdict_cancel_t cancel, void *user) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	char folder[DICT_PATH];
	if (stopped(cancel, user)) { out->cancelled = true; return; }
	if (!root_path(root, folder)) { out->unavailable = true; return; }
	DIR *dir = opendir(folder);
	if (!dir) { out->unavailable = true; return; }
	unsigned visited = 0;
	struct dirent *de;
	while ((de = readdir(dir))) {
		if (stopped(cancel, user)) { out->cancelled = true; break; }
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		if (++visited > 64) { out->truncated = true; break; }
		if (!valid_id(de->d_name)) continue;
		char file[DICT_PATH];
		int n = snprintf(file, sizeof(file), "%s/%s", folder, de->d_name);
		struct stat st;
		if (n < 0 || (size_t)n >= sizeof(file) || !ordinary(file, &st) || !S_ISREG(st.st_mode) ||
			st.st_size < HEADER || (uint64_t)st.st_size > EBOOKDICT_MAX_FILE) { out->skipped++; continue; }
		ebookdict_entry_t entry;
		size_t length = strlen(de->d_name);
		memcpy(entry.id, de->d_name, length + 1);
		memcpy(entry.name, de->d_name, length - 4); entry.name[length - 4] = 0;
		unsigned at = out->count;
		if (at == EBOOKDICT_MAX) { out->truncated = true; at--; }
		else out->count++;
		if (out->truncated && strcasecmp(entry.name, out->entries[at].name) > 0) continue;
		while (at && strcasecmp(entry.name, out->entries[at - 1].name) < 0) {
			out->entries[at] = out->entries[at - 1]; at--;
		}
		out->entries[at] = entry;
	}
	closedir(dir);
}

static bool space(uint32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xa0 || c == '_'; }
static bool edge(uint32_t c) {
	return space(c) || (c < 128 && strchr(".,;:!?()[]{}\"'", (int)c)) ||
		c == 0x2018 || c == 0x2019 || c == 0x201c || c == 0x201d;
}
static size_t encode(uint32_t c, char *out) {
	if (c < 128) { out[0] = (char)c; return 1; }
	if (c < 0x800) { out[0] = (char)(0xc0 | (c >> 6)); out[1] = (char)(0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { out[0] = (char)(0xe0 | (c >> 12)); out[1] = (char)(0x80 | ((c >> 6) & 63)); out[2] = (char)(0x80 | (c & 63)); return 3; }
	out[0] = (char)(0xf0 | (c >> 18)); out[1] = (char)(0x80 | ((c >> 12) & 63));
	out[2] = (char)(0x80 | ((c >> 6) & 63)); out[3] = (char)(0x80 | (c & 63)); return 4;
}

ebookdict_status_t ebookdict_normalize(const char *query, char out[EBOOKDICT_WORD]) {
	out[0] = 0;
	if (!query) return EBOOKDICT_EMPTY;
	size_t n = strnlen(query, EBOOKDICT_QUERY);
	if (n == EBOOKDICT_QUERY) return EBOOKDICT_LONG_QUERY;
	uint32_t codes[EBOOKDICT_QUERY];
	size_t count = 0, offset = 0;
	while (offset < n) {
		uint32_t c = ebooktext_decode(query, n, &offset);
		if (c == 0xfffd || c == 127 || (c < 32 && !space(c))) return EBOOKDICT_BAD_QUERY;
		codes[count++] = c;
	}
	size_t first = 0, last = count;
	while (first < last && edge(codes[first])) first++;
	while (last > first && edge(codes[last - 1])) last--;
	size_t used = 0;
	bool gap = false;
	for (size_t i = first; i < last; i++) {
		uint32_t c = codes[i];
		if (space(c)) { gap = used > 0; continue; }
		if (c == 0x2019) c = '\'';
		if (c == 0x2010 || c == 0x2011) c = '-';
		char bytes[4]; size_t length = encode(ebooktext_fold(c), bytes);
		if (used + (gap ? 1 : 0) + length >= EBOOKDICT_WORD) { out[0] = 0; return EBOOKDICT_LONG_QUERY; }
		if (gap) out[used++] = ' ';
		memcpy(out + used, bytes, length); used += length; gap = false;
	}
	out[used] = 0;
	return used ? EBOOKDICT_OK : EBOOKDICT_EMPTY;
}

static uint32_t le32(const unsigned char *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool read_record(FILE *file, uint32_t index, uint32_t data_start, uint32_t size,
	unsigned char record[RECORD], ebookdict_result_t *out) {
	out->probes++;
	if (fseek(file, (long)(HEADER + index * RECORD), SEEK_SET) || fread(record, 1, RECORD, file) != RECORD) return false;
	if (!record[0] || !memchr(record, 0, EBOOKDICT_WORD)) return false;
	char normalized[EBOOKDICT_WORD];
	if (ebookdict_normalize((const char *)record, normalized) != EBOOKDICT_OK || strcmp(normalized, (const char *)record)) return false;
	uint32_t offset = le32(record + EBOOKDICT_WORD), length = le32(record + EBOOKDICT_WORD + 4);
	return offset >= data_start && offset <= size && length && length <= EBOOKDICT_MAX_DEFINITION && length <= size - offset;
}

/* Trim only an incomplete final scalar of a clipped definition. Other invalid
 * UTF-8, embedded NULs and control bytes make the record unreadable. */
static bool definition(ebookdict_result_t *out, size_t n) {
	size_t at = 0;
	while (at < n) {
		size_t start = at;
		unsigned char lead = (unsigned char)out->text[start];
		unsigned length = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
			lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
		if (!length) return false;
		if (length > n - at && out->truncated) { out->text[at] = 0; return true; }
		uint32_t c = ebooktext_decode(out->text, n, &at);
		if ((c == 0xfffd && at - start != 3) || !c || c == 127 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) return false;
	}
	return true;
}

void ebookdict_lookup(const char *root, const char *id, const char *query,
	ebookdict_result_t *out, ebookdict_cancel_t cancel, void *user) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	out->status = ebookdict_normalize(query, out->word);
	if (out->status != EBOOKDICT_OK) return;
	if (stopped(cancel, user)) { out->status = EBOOKDICT_CANCELLED; return; }
	out->status = EBOOKDICT_UNAVAILABLE;
	char folder[DICT_PATH], file_path[DICT_PATH];
	if (!valid_id(id) || !root_path(root, folder)) return;
	int n = snprintf(file_path, sizeof(file_path), "%s/%s", folder, id);
	struct stat st;
	if (n < 0 || (size_t)n >= sizeof(file_path) || !ordinary(file_path, &st) || !S_ISREG(st.st_mode)) return;
	out->status = EBOOKDICT_BAD_FILE;
	if (st.st_size < HEADER || (uint64_t)st.st_size > EBOOKDICT_MAX_FILE) return;
	FILE *file = fopen(file_path, "rb");
	if (!file) { out->status = EBOOKDICT_UNAVAILABLE; return; }
	unsigned char header[HEADER], record[RECORD];
	if (fread(header, 1, HEADER, file) != HEADER || memcmp(header, "SNXDICT1", 8) || le32(header + 12) != RECORD) goto done;
	uint32_t count = le32(header + 8), size = (uint32_t)st.st_size;
	if (!count || count > EBOOKDICT_MAX_ENTRIES || (uint64_t)HEADER + (uint64_t)count * RECORD > size) goto done;
	uint32_t data_start = HEADER + count * RECORD, lo = 0, hi = count;
	out->status = EBOOKDICT_NOT_FOUND;
	while (lo < hi) {
		if (stopped(cancel, user)) { out->status = EBOOKDICT_CANCELLED; goto done; }
		uint32_t mid = lo + (hi - lo) / 2;
		if (!read_record(file, mid, data_start, size, record, out)) { out->status = EBOOKDICT_BAD_FILE; goto done; }
		int cmp = strcmp(out->word, (const char *)record);
		if (cmp < 0) hi = mid;
		else if (cmp > 0) lo = mid + 1;
		else {
			uint32_t offset = le32(record + EBOOKDICT_WORD), length = le32(record + EBOOKDICT_WORD + 4);
			size_t take = length < EBOOKDICT_TEXT ? length : EBOOKDICT_TEXT;
			out->truncated = length > take;
			if (fseek(file, (long)offset, SEEK_SET) || fread(out->text, 1, take, file) != take) {
				out->status = EBOOKDICT_BAD_FILE; goto done;
			}
			out->text[take] = 0;
			out->status = definition(out, take) ? EBOOKDICT_OK : EBOOKDICT_BAD_FILE;
			break;
		}
	}
	done:
	fclose(file);
	if (stopped(cancel, user)) out->status = EBOOKDICT_CANCELLED;
	if (out->status != EBOOKDICT_OK) { out->text[0] = 0; out->truncated = false; }
}
