#define _POSIX_C_SOURCE 200809L
#include "ebookfontlist.h"
#include "src/system/device/sdpackages.h"

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

static bool ordinary(const char *path, struct stat *st) {
#ifdef _WIN32
	DWORD a = GetFileAttributesA(path);
	if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
	return lstat(path, st) == 0 && (S_ISDIR(st->st_mode) || S_ISREG(st->st_mode));
}
static bool extension(const char *id) {
	size_t n = strlen(id);
	return n > 4 && (!strcasecmp(id + n - 4, ".ttf") || !strcasecmp(id + n - 4, ".otf"));
}
bool ebookfontlist_valid_id(const char *id) {
	if (!id || !id[0] || strlen(id) >= EBOOKFONTLIST_ID || !extension(id)) return false;
	unsigned components = 1;
	const char *part = id;
	for (const char *p = id;; p++) {
		if (*p == '\\' || *p == ':' || (*p && (unsigned char)*p < 32)) return false;
		if (*p == '/' || !*p) {
			size_t n = (size_t)(p - part);
			if (!n || part[0] == '.' || (n == 2 && !strncmp(part, "..", 2))) return false;
			if (!*p) break;
			if (++components > 2) return false;
			part = p + 1;
		}
	}
	return true;
}
static bool root_path(const char *root, char *out, size_t size) {
	if (!root || !root[0]) return false;
	int n = snprintf(out, size, "%s/Ebook", root);
	struct stat st;
	if (n < 0 || (size_t)n >= size || !ordinary(out, &st) || !S_ISDIR(st.st_mode)) return false;
	size_t used = strlen(out);
	if (used + sizeof("/Fonts") > size) return false;
	memcpy(out + used, "/Fonts", sizeof("/Fonts"));
	return ordinary(out, &st) && S_ISDIR(st.st_mode);
}
static uint32_t be32(const unsigned char *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static bool valid_file(const char *path) {
	struct stat st;
	if (!ordinary(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 12 ||
		(uint64_t)st.st_size > EBOOKFONTLIST_MAX_BYTES) return false;
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	unsigned char head[12], table[16];
	bool ok = fread(head, 1, sizeof(head), f) == sizeof(head);
	unsigned tables = ok ? ((unsigned)head[4] << 8) | head[5] : 0;
	ok = ok && (be32(head) == 0x00010000u || !memcmp(head, "OTTO", 4) || !memcmp(head, "true", 4)) &&
		tables > 0 && tables <= 4096 && 12u + 16u * tables <= (uint64_t)st.st_size;
	for (unsigned i = 0; ok && i < tables; i++) {
		ok = fread(table, 1, sizeof(table), f) == sizeof(table);
		if (ok) {
			uint32_t offset = be32(table + 8), length = be32(table + 12);
			ok = offset <= (uint64_t)st.st_size && length <= (uint64_t)st.st_size - offset;
		}
	}
	fclose(f);
	return ok;
}

static bool resolve_in(const char *root, const char *id, unsigned style, char *out, size_t size) {
	if (!out || !size) return false;
	out[0] = 0;
	if (!ebookfontlist_valid_id(id) || style > 3) return false;
	char candidate[EBOOKFONTLIST_ID];
	const char *slash = strchr(id, '/');
	if (slash) {
		int n = snprintf(out, size, "%s/%.*s", root, (int)(slash - id), id);
		struct stat st;
		if (n < 0 || (size_t)n >= size || !ordinary(out, &st) || !S_ISDIR(st.st_mode)) { out[0] = 0; return false; }
	}
	if (!style) snprintf(candidate, sizeof(candidate), "%s", id);
	else {
		if (!slash || (strcmp(slash + 1, "Regular.ttf") && strcmp(slash + 1, "Regular.otf"))) { out[0] = 0; return false; }
		static const char *const names[] = {"Regular", "Italic", "Bold", "BoldItalic"};
		for (unsigned ext = 0; ext < 2; ext++) {
			int n = snprintf(out, size, "%s/%.*s/%s.%s", root, (int)(slash - id), id, names[style], ext ? "otf" : "ttf");
			if (n > 0 && (size_t)n < size && valid_file(out)) return true;
		}
		out[0] = 0; return false;
	}
	int n = snprintf(out, size, "%s/%s", root, candidate);
	if (n > 0 && (size_t)n < size && valid_file(out)) return true;
	out[0] = 0;
	return false;
}

bool ebookfontlist_resolve(const char *sd_root, const char *id, unsigned style, char *out, size_t size) {
	char folder[EBOOKFONTLIST_PATH];
	if (root_path(sd_root, folder, sizeof(folder)) && resolve_in(folder, id, style, out, size)) return true;
	if (sdpackages_path(sd_root, "Fonts", folder, sizeof(folder)) && resolve_in(folder, id, style, out, size)) return true;
	if (out && size) out[0] = 0;
	return false;
}

void ebookfontlist_name(const char *id, char *out, size_t size) {
	if (!out || !size) return;
	if (!id || !id[0]) { snprintf(out, size, "Default"); return; }
	const char *slash = strchr(id, '/');
	size_t n = slash ? (size_t)(slash - id) : strlen(id);
	if (!slash && n > 4) n -= 4;
	if (n >= size) n = size - 1;
	while (n && ((unsigned char)id[n] & 0xc0) == 0x80) n--;
	memcpy(out, id, n); out[n] = 0;
}

static void add(ebookfontlist_t *out, const char *root, const char *id) {
	for (unsigned i = 0; i < (unsigned)out->count; i++) if (!strcmp(out->entries[i].id, id)) return;
	char resolved[EBOOKFONTLIST_PATH];
	if (!ebookfontlist_resolve(root, id, 0, resolved, sizeof(resolved))) { out->skipped++; return; }
	ebookfontlist_entry_t entry;
	snprintf(entry.id, sizeof(entry.id), "%s", id);
	ebookfontlist_name(id, entry.name, sizeof(entry.name));
	int at = out->count;
	if (at == EBOOKFONTLIST_MAX) { out->truncated = true; at--; }
	else out->count++;
	if (out->truncated && strcasecmp(entry.name, out->entries[at].name) > 0) return;
	while (at && strcasecmp(entry.name, out->entries[at - 1].name) < 0) {
		out->entries[at] = out->entries[at - 1]; at--;
	}
	out->entries[at] = entry;
}

static void scan_in(const char *sd_root, const char *root, ebookfontlist_t *out) {
	DIR *dir = opendir(root);
	if (!dir) { out->unavailable = true; return; }
	struct dirent *de;
	unsigned visited = 0;
	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		if (++visited > 256) { out->truncated = true; break; }
		if (de->d_name[0] == '.') continue;
		char file[EBOOKFONTLIST_PATH];
		int n = snprintf(file, sizeof(file), "%s/%s", root, de->d_name);
		struct stat st;
		if (n < 0 || (size_t)n >= sizeof(file) || !ordinary(file, &st)) { out->skipped++; continue; }
		if (S_ISDIR(st.st_mode)) {
			char id[EBOOKFONTLIST_ID], resolved[EBOOKFONTLIST_PATH];
			size_t family_len = strlen(de->d_name);
			if (family_len + sizeof("/Regular.ttf") > sizeof(id)) { out->skipped++; continue; }
			memcpy(id, de->d_name, family_len);
			memcpy(id + family_len, "/Regular.ttf", sizeof("/Regular.ttf"));
			if (!ebookfontlist_resolve(sd_root, id, 0, resolved, sizeof(resolved)))
				memcpy(id + family_len, "/Regular.otf", sizeof("/Regular.otf"));
			add(out, sd_root, id);
		} else if (extension(de->d_name)) add(out, sd_root, de->d_name);
	}
	closedir(dir);
}

void ebookfontlist_scan(const char *sd_root, ebookfontlist_t *out) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	char folder[EBOOKFONTLIST_PATH]; bool found = false;
	if (root_path(sd_root, folder, sizeof(folder))) { found = true; scan_in(sd_root, folder, out); }
	if (sdpackages_path(sd_root, "Fonts", folder, sizeof(folder))) { found = true; scan_in(sd_root, folder, out); }
	if (!found) out->unavailable = true;
}
