#define _POSIX_C_SOURCE 200809L
#include "sdpackages.h"
#include <stdio.h>
#include <stdlib.h>
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
static const char *const EXTRA_ASSETS[] = {"Sonix-Books-Extra-Fonts.spk", "Sonix-Books-Extra-Dictionary.spk", "Sonix-Books-Extra-Readers.spk"};
static const char *const EXTRA_FOLDERS[] = {"Fonts", "Dictionaries", "Tools"};
static const char *const EXTRA_BASES[] = {"packages-fonts", "packages-dictionaries", "packages-tools"};
const char *sdpackages_extra_asset(unsigned category) { return category < SD_EXTRAS_COUNT ? EXTRA_ASSETS[category] : NULL; }
const char *sdpackages_extra_folder(unsigned category) { return category < SD_EXTRAS_COUNT ? EXTRA_FOLDERS[category] : NULL; }
const char *sdpackages_extra_base(unsigned category) { return category < SD_EXTRAS_COUNT ? EXTRA_BASES[category] : NULL; }

static bool base(const char *root, const char *name, char *out, size_t size) {
	if (!root || !root[0]) return false;
	int n = snprintf(out, size, "%s/.sonix", root);
	if (n < 0 || (size_t)n >= size || !ordinary(out, true)) return false;
	size_t used = (size_t)n, len = strlen(name);
	if (used + len + 2 > size) return false;
	out[used] = '/'; memcpy(out + used + 1, name, len + 1);
	return ordinary(out, true);
}
static bool generation_in(const char *root, const char *name, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]) {
	version[0] = 0;
	generation[0] = 0;
	char folder[1000], path[1100], line[96];
	if (!base(root, name, folder, sizeof(folder))) return false;
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
bool sdpackages_generation(const char *root, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]) {
	return generation_in(root, "packages", generation, version);
}
bool sdpackages_extra_generation(const char *root, unsigned category, char generation[SDPACKAGES_VERSION], char version[SDPACKAGES_VERSION]) {
	const char *name = sdpackages_extra_base(category);
	return name && generation_in(root, name, generation, version);
}
bool sdpackages_active(const char *root, char version[SDPACKAGES_VERSION]) {
	char generation[SDPACKAGES_VERSION];
	return sdpackages_generation(root, generation, version);
}
static bool path_in(const char *root, const char *name, const char *folder, char *out, size_t size) {
	if (!out || !size) return false;
	out[0] = 0;
	if (!folder || (strcmp(folder, "Fonts") && strcmp(folder, "Dictionaries") && strcmp(folder, "Tools"))) return false;
	char version[SDPACKAGES_VERSION], generation[SDPACKAGES_VERSION], managed[1000], path[1200];
	if (!generation_in(root, name, generation, version) || !base(root, name, managed, sizeof(managed))) return false;
	int n = snprintf(path, sizeof(path), "%s/%s/Ebook", managed, generation);
	if (n < 0 || (size_t)n >= sizeof(path) || !ordinary(path, true)) return false;
	size_t used = (size_t)n, len = strlen(folder);
	if (used + len + 2 > sizeof(path)) return false;
	path[used] = '/'; memcpy(path + used + 1, folder, len + 1);
	if (!ordinary(path, true) || strlen(path) >= size) return false;
	memcpy(out, path, strlen(path) + 1);
	return true;
}

// Scoped extras override the older combined bundle only for the same or newer
// numeric release. A newer firmware bundle must not be shadowed by old tools.
static bool version_at_least(const char *a, const char *b) {
	if (!strcmp(a,b)) return true;
	if (*a == 'v') ++a;
	if (*b == 'v') ++b;
	for (unsigned i = 0; i < 4; ++i) {
		char *ae, *be;
		unsigned long av = strtoul(a, &ae, 10), bv = strtoul(b, &be, 10);
		if (ae == a || be == b) return false;
		if (av != bv) return av > bv;
		if (*ae != '.' || *be != '.') return *ae == '.' && *be != '.';
		a = ae + 1; b = be + 1;
	}
	return true;
}
bool sdpackages_path(const char *root, const char *folder, char *out, size_t size) {
	if (!out || !size || !folder) return false;
	out[0] = 0;
	char combined[SDPACKAGES_VERSION], generation[SDPACKAGES_VERSION], scoped[SDPACKAGES_VERSION];
	bool have_combined = sdpackages_active(root, combined);
	for (unsigned i = 0; i < SD_EXTRAS_COUNT; ++i) {
		if (strcmp(folder, sdpackages_extra_folder(i))) continue;
		if (sdpackages_extra_generation(root, i, generation, scoped) &&
			(!have_combined || version_at_least(scoped, combined)) && path_in(root, sdpackages_extra_base(i), folder, out, size)) return true;
		break;
	}
	return path_in(root, "packages", folder, out, size);
}

