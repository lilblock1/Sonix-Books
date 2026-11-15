#include "ota_policy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdpackages.h"

bool ota_policy_package(const char *repo, const char *tag, const char *url, long size) {
	if (!repo || !repo[0] || !sdpackages_valid_version(tag) || !url || size <= 0 || (unsigned long)size > SDPACKAGES_MAX_BYTES) return false;
	char expected[512];
	int n = snprintf(expected, sizeof(expected), "https://github.com/%s/releases/download/%s/%s", repo, tag, SDPACKAGES_ASSET);
	return n > 0 && (size_t)n < sizeof(expected) && !strcmp(url, expected);
}

bool ota_policy_digest(const char *digest) {
	if (!digest || strlen(digest) != 71 || strncmp(digest, "sha256:", 7) != 0) {
		return false;
	}
	for (int i = 7; i < 71; i++) {
		if (!isxdigit((unsigned char)digest[i])) {
			return false;
		}
	}
	return true;
}

bool ota_policy_asset(const char *repo, const char *stem, const char *url, long size) {
	if (!repo || !*repo || !stem || !*stem || !url || size <= 0 || size > 128L * 1024 * 1024) {
		return false;
	}
	char prefix[256], suffix[96];
	int n = snprintf(prefix, sizeof(prefix), "https://github.com/%s/releases/download/", repo);
	int s = snprintf(suffix, sizeof(suffix), "/%s.upt", stem);
	if (n < 0 || (size_t)n >= sizeof(prefix) || s < 0 || (size_t)s >= sizeof(suffix)) {
		return false;
	}
	size_t length = strlen(url);
	if (length <= (size_t)n + (size_t)s || strncmp(url, prefix, (size_t)n) != 0 ||
		strcmp(url + length - (size_t)s, suffix) != 0) {
		return false;
	}
	// GitHub's canonical asset URL has a single tag segment. Reject query
	// strings, fragments and extra path components rather than guessing.
	for (const char *p = url + n; p < url + length - (size_t)s; p++) {
		if (*p == '/' || *p == '?' || *p == '#' || isspace((unsigned char)*p)) {
			return false;
		}
	}
	return true;
}

bool ota_policy_battery(const char *percent) {
	if (!percent || !isdigit((unsigned char)*percent)) {
		return false;
	}
	char *end;
	long level = strtol(percent, &end, 10);
	return *end == '\0' && level >= 50 && level <= 100;
}

bool ota_policy_version(const char *text, long out[4]) {
	if (!text || !out) return false;
	memset(out, 0, 4 * sizeof(*out));
	const char *p = text;
	if (*p == 'v' || *p == 'V') p++;
	int parts = 0;
	while (parts < 4) {
		if (!isdigit((unsigned char)*p)) return false;
		long value = 0;
		while (isdigit((unsigned char)*p)) {
			if (value > 100000000 || (value == 100000000 && *p > '0')) return false;
			value = value * 10 + (*p++ - '0');
		}
		out[parts++] = value;
		if (*p != '.') break;
		if (parts == 4) return false;
		p++;
	}
	return parts >= 3 && (!*p || (parts == 4 && !strcmp(p, "-test")));
}
int ota_policy_version_cmp(const long a[4], const long b[4]) {
	for (int i = 0; i < 4; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
	return 0;
}
bool ota_policy_release(bool test_channel, bool draft, bool prerelease, const char *tag) {
	long version[4];
	if (draft || !ota_policy_version(tag, version)) return false;
	bool test = strlen(tag) >= 5 && !strcmp(tag + strlen(tag) - 5, "-test");
	return prerelease ? test_channel && test : !test;
}
