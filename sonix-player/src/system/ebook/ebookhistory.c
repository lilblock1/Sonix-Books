#include "ebookhistory.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "src/system/core/config.h"
#include "src/system/ebook/bookmarks.h"

void ebookhistory_remember(const char *path) {
	if (!path || !path[0]) {
		return;
	}
	char key[24];
	bookmark_key(path, key, sizeof(key));
	config_store_set(config_ebook_store(), "ebook", "last_book", key);
	config_store_save(config_ebook_store());
}

bool ebookhistory_is_last(const char *path) {
	if (!path || !path[0]) {
		return false;
	}
	char key[24];
	bookmark_key(path, key, sizeof(key));
	const char *last = config_store_get(config_ebook_store(), "ebook", "last_book", "");
	return strcmp(last, key) == 0;
}

bool ebookhistory_find_last(const char *folder, char *out, size_t out_size) {
	if (!out || out_size == 0) {
		return false;
	}
	out[0] = '\0';
	if (!folder || !folder[0] ||
		!config_store_get(config_ebook_store(), "ebook", "last_book", "")[0]) {
		return false;
	}
	DIR *dir = opendir(folder);
	if (!dir) {
		return false;
	}
	bool found = false;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		size_t length = strlen(entry->d_name);
		if (length <= 5 || strcasecmp(entry->d_name + length - 5, ".epub") != 0) {
			continue;
		}
		int written = snprintf(out, out_size, "%s/%s", folder, entry->d_name);
		if (written < 0 || (size_t)written >= out_size || !ebookhistory_is_last(out)) {
			continue;
		}
		struct stat st;
		if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) {
			found = true;
			break;
		}
	}
	closedir(dir);
	if (!found) {
		out[0] = '\0';
	}
	return found;
}
