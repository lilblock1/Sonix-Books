#define _POSIX_C_SOURCE 200809L
#include "sdpackages.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#define lstat stat
#endif

static bool ordinary(const char *path, bool directory) {
#ifdef _WIN32
	DWORD attrs = GetFileAttributesA(path);
	if (attrs == INVALID_FILE_ATTRIBUTES || attrs & FILE_ATTRIBUTE_REPARSE_POINT) return false;
#endif
	struct stat st;
	return !lstat(path, &st) && (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}
bool sdpackages_valid_version(const char *version) {
	if (!version || !version[0] || strlen(version) >= SDPACKAGES_VERSION ||
		!((version[0] >= '0' && version[0] <= '9') || version[0] == 'v') || strstr(version, "..")) return false;
	for (const char *p = version; *p; p++)
		if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '.' || *p == '-')) return false;
	return true;
}
static bool base(const char *root, char *out, size_t size) {
	if (!root || !root[0]) return false;
	int n = snprintf(out, size, "%s/.sonix", root);
	if (n < 0 || (size_t)n >= size || !ordinary(out, true)) return false;
	size_t used = (size_t)n;
	if (used + sizeof("/packages") > size) return false;
	memcpy(out + used, "/packages", sizeof("/packages"));
	return ordinary(out, true);
}
bool sdpackages_generation(const char *root, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]) {
	version[0] = 0;
	generation[0] = 0;
	char folder[1000], path[1100], line[96];
	if (!base(root, folder, sizeof(folder))) return false;
	int n = snprintf(path, sizeof(path), "%s/active", folder);
	if (n < 0 || (size_t)n >= sizeof(path) || !ordinary(path, false)) return false;
	FILE *f = fopen(path, "r"); if (!f) return false;
	bool ok = fgets(line, sizeof(line), f) != NULL;
	int trailing = fgetc(f); fclose(f);
	if (!ok || trailing != EOF) return false;
	line[strcspn(line, "\r\n")] = 0;
	if (!sdpackages_valid_version(line)) return false;
	n = snprintf(path, sizeof(path), "%s/%s", folder, line);
	if (n < 0 || (size_t)n >= sizeof(path) || !ordinary(path, true)) return false;
	size_t used = (size_t)n;
	if (used + sizeof("/.complete") > sizeof(path)) return false;
	memcpy(path + used, "/.complete", sizeof("/.complete"));
	if (!ordinary(path, false)) return false;
	f = fopen(path, "r"); if (!f) return false;
	char marker[96];
	ok = fgets(marker, sizeof(marker), f) && !strncmp(marker, "SDPKG1 ", 7) && fgetc(f) == EOF;
	fclose(f);
	if (ok) {
		marker[strcspn(marker, "\r\n")] = 0;
		ok = sdpackages_valid_version(marker + 7);
		if (ok) { memcpy(version, marker + 7, strlen(marker + 7) + 1); memcpy(generation, line, strlen(line) + 1); }
	}
	return ok;
}
bool sdpackages_active(const char *root, char version[SDPACKAGES_VERSION]) {
	char generation[SDPACKAGES_VERSION];
	return sdpackages_generation(root, generation, version);
}
bool sdpackages_path(const char *root, const char *folder, char *out, size_t size) {
	if (!out || !size) return false;
	out[0] = 0;
	if (!folder || (strcmp(folder, "Fonts") && strcmp(folder, "Dictionaries") && strcmp(folder, "Tools"))) return false;
	char version[SDPACKAGES_VERSION], generation[SDPACKAGES_VERSION], managed[1000], path[1200];
	if (!sdpackages_generation(root, generation, version) || !base(root, managed, sizeof(managed))) return false;
	int n = snprintf(path, sizeof(path), "%s/%s/Ebook", managed, generation);
	if (n < 0 || (size_t)n >= sizeof(path) || !ordinary(path, true)) return false;
	size_t used = (size_t)n, len = strlen(folder);
	if (used + len + 2 > sizeof(path)) return false;
	path[used] = '/'; memcpy(path + used + 1, folder, len + 1);
	if (!ordinary(path, true) || strlen(path) >= size) return false;
	memcpy(out, path, strlen(path) + 1);
	return true;
}
