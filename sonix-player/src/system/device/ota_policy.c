#include "ota_policy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
