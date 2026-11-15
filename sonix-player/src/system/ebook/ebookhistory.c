#include "ebookhistory.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "src/system/core/config.h"
#include "src/system/ebook/bookmarks.h"

/* Six 16-digit hashes plus commas fit in the config's 128-byte value. Only
 * two fixed entries are needed, regardless of how many books get opened. */
#define KEYS_PER_VALUE 6

static void append_key(ebookhistory_list_t *out, const char *text, size_t length) {
	if (!text || length != EBOOKHISTORY_KEY_SIZE - 1 || out->count >= EBOOKHISTORY_MAX) return;
	char key[EBOOKHISTORY_KEY_SIZE];
	for (size_t i = 0; i < length; i++) {
		char c = text[i];
		if (c >= 'A' && c <= 'F') c += 'a' - 'A';
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return;
		key[i] = c;
	}
	key[length] = 0;
	for (int i = 0; i < out->count; i++) if (!strcmp(out->keys[i], key)) return;
	memcpy(out->keys[out->count++], key, sizeof(key));
}

void ebookhistory_read(ebookhistory_list_t *out) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	config_store_t *store = config_ebook_store();
	const char *last = config_store_get(store, "ebook", "last_book", "");
	append_key(out, last, strlen(last));
	for (int part = 0; part < EBOOKHISTORY_MAX / KEYS_PER_VALUE; part++) {
		char name[24];
		snprintf(name, sizeof(name), "recent_v1_%d", part);
		const char *p = config_store_get(store, "ebook", name, "");
		while (*p) {
			const char *end = strchr(p, ',');
			append_key(out, p, end ? (size_t)(end - p) : strlen(p));
			if (!end) break;
			p = end + 1;
		}
	}
}

int ebookhistory_rank(const ebookhistory_list_t *list, const char *path) {
	if (!list || !path || !path[0]) return -1;
	char key[EBOOKHISTORY_KEY_SIZE];
	bookmark_key(path, key, sizeof(key));
	int count = list->count < EBOOKHISTORY_MAX ? list->count : EBOOKHISTORY_MAX;
	for (int i = 0; i < count; i++) if (!memcmp(list->keys[i], key, sizeof(key))) return i;
	return -1;
}

void ebookhistory_remember(const char *path) {
	if (!path || !path[0]) {
		return;
	}
	ebookhistory_list_t old, list = {0};
	ebookhistory_read(&old);
	char key[EBOOKHISTORY_KEY_SIZE];
	bookmark_key(path, key, sizeof(key));
	append_key(&list, key, strlen(key));
	for (int i = 0; i < old.count; i++) append_key(&list, old.keys[i], strlen(old.keys[i]));
	config_store_t *store = config_ebook_store();
	config_store_set(store, "ebook", "last_book", key); /* retained for older firmware */
	for (int part = 0; part < EBOOKHISTORY_MAX / KEYS_PER_VALUE; part++) {
		char name[24], value[128] = "";
		snprintf(name, sizeof(name), "recent_v1_%d", part);
		for (int i = part * KEYS_PER_VALUE; i < list.count && i < (part + 1) * KEYS_PER_VALUE; i++) {
			if (value[0]) strcat(value, ",");
			strcat(value, list.keys[i]);
		}
		config_store_set(store, "ebook", name, value);
	}
	config_store_save(store);
}

bool ebookhistory_is_last(const char *path) {
	if (!path || !path[0]) {
		return false;
	}
	ebookhistory_list_t list;
	ebookhistory_read(&list);
	return ebookhistory_rank(&list, path) == 0;
}

bool ebookhistory_find_last(const char *folder, char *out, size_t out_size) {
	if (!out || out_size == 0) {
		return false;
	}
	out[0] = '\0';
	ebookhistory_list_t list;
	ebookhistory_read(&list);
	if (!folder || !folder[0] || !list.count) {
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
		if (written < 0 || (size_t)written >= out_size || ebookhistory_rank(&list, out) != 0) {
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
