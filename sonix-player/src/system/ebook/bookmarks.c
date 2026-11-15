#include "src/system/ebook/bookmarks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/system/core/config.h"

#define SECTION "bookmarks"
#define NOTE_CHUNK 60
#define NOTE_CHUNKS ((BOOKMARK_NOTE_MAX - 1) / NOTE_CHUNK)
#define KEYS_PER_MARK (3 + NOTE_CHUNKS)

void bookmark_key(const char *book_path, char *out, size_t out_size) {
	uint64_t h = 1469598103934665603ull;
	for (const unsigned char *p = (const unsigned char *)(book_path ? book_path : ""); *p; p++) {
		h ^= *p;
		h *= 1099511628211ull;
	}
	snprintf(out, out_size, "%08x%08x", (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
}

static void count_key(const char *path, char *out, size_t size) {
	char hash[17];
	bookmark_key(path, hash, sizeof(hash));
	snprintf(out, size, "n_%s", hash);
}

static void entry_key(const char *path, int index, char *out, size_t size) {
	char hash[17];
	bookmark_key(path, hash, sizeof(hash));
	snprintf(out, size, "b_%s_%d", hash, index);
}

/* Length of a complete, valid UTF-8 scalar, or zero. Never read past n. */
static size_t scalar(const unsigned char *p, size_t n) {
	if (!n) return 0;
	if (p[0] < 0x80) return 1;
	unsigned len = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
		p[0] >= 0xe0 && p[0] <= 0xef ? 3 : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
	if (!len || len > n) return 0;
	for (unsigned i = 1; i < len; i++) if ((p[i] & 0xc0) != 0x80) return 0;
	if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
		(p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
	return len;
}

static bool valid_text(const char *text, size_t size, bool multiline) {
	const char *end = memchr(text, 0, size);
	if (!end) return false;
	size_t n = (size_t)(end - text);
	for (size_t i = 0; i < n;) {
		unsigned char c = (unsigned char)text[i];
		size_t len = scalar((const unsigned char *)text + i, n - i);
		if (!len || c == 127 || (c < 32 && !(multiline && (c == '\n' || c == '\r' || c == '\t'))))
			return false;
		i += len;
	}
	return true;
}

/* Snippets from older builds may end halfway through a character. Preserve
 * their complete prefix and make every snippet safe for the legacy INI line. */
static void snippet(char *out, size_t size, const char *text) {
	size_t n = strlen(text), used = 0;
	for (size_t i = 0; i < n;) {
		size_t len = scalar((const unsigned char *)text + i, n - i);
		if (!len || used + len >= size) break;
		if (len == 1 && ((unsigned char)text[i] < 32 || text[i] == 127)) out[used++] = ' ';
		else { memcpy(out + used, text + i, len); used += len; }
		i += len;
	}
	out[used] = 0;
}

int bookmark_count(const char *path) {
	if (!path || !path[0]) return 0;
	char key[32];
	count_key(path, key, sizeof(key));
	long n = config_store_get_int(config_ebook_store(), SECTION, key, 0);
	return n < 0 ? 0 : n > BOOKMARK_MAX_PER_BOOK ? BOOKMARK_MAX_PER_BOOK : (int)n;
}

static bool number(const char **cursor, uint32_t *out, char separator) {
	const char *p = *cursor;
	if (*p < '0' || *p > '9') return false;
	uint32_t n = 0;
	while (*p >= '0' && *p <= '9') {
		unsigned digit = (unsigned)(*p++ - '0');
		if (n > (UINT32_MAX - digit) / 10u) return false;
		n = n * 10u + digit;
	}
	if (separator ? *p != separator : (*p && *p != '|')) return false;
	*out = n;
	*cursor = separator ? p + 1 : p;
	return true;
}

static int unhex(char c) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static bool decode(const char *value, char *out, size_t size) {
	size_t n = strlen(value);
	if (n % 2 || n / 2 >= size) return false;
	for (size_t i = 0; i < n; i += 2) {
		int a = unhex(value[i]), b = unhex(value[i + 1]);
		if (a < 0 || b < 0 || !(a || b)) return false;
		out[i / 2] = (char)((a << 4) | b);
	}
	out[n / 2] = 0;
	return true;
}
static void encode(const char *text, size_t n, char *out) {
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)text[i];
		out[2 * i] = hex[c >> 4];
		out[2 * i + 1] = hex[c & 15];
	}
	out[2 * n] = 0;
}

bool bookmark_get(const char *path, int index, bookmark_t *out) {
	if (!out) return false;
	memset(out, 0, sizeof(*out));
	if (index < 0 || index >= bookmark_count(path)) return false;
	char key[32], hash[17];
	entry_key(path, index, key, sizeof(key));
	const char *p = config_store_get(config_ebook_store(), SECTION, key, "");
	if (!number(&p, &out->spine, ':') || !number(&p, &out->block, ':') ||
		!number(&p, &out->offset, 0)) { memset(out, 0, sizeof(*out)); return false; }
	if (*p == '|') snippet(out->text, sizeof(out->text), p + 1);
	bookmark_key(path, hash, sizeof(hash));
	// An older firmware can move the legacy keys when deleting a bookmark.
	// Never attach an annotation to a different position after a downgrade.
	char position[40];
	snprintf(position, sizeof(position), "%u:%u:%u", out->spine, out->block, out->offset);
	snprintf(key, sizeof(key), "p_%s_%d", hash, index);
	if (strcmp(position, config_store_get(config_ebook_store(), SECTION, key, ""))) return true;
	snprintf(key, sizeof(key), "t_%s_%d", hash, index);
	if (!decode(config_store_get(config_ebook_store(), SECTION, key, ""), out->name, sizeof(out->name)) ||
		!valid_text(out->name, sizeof(out->name), false)) out->name[0] = 0;
	char note[BOOKMARK_NOTE_MAX * 2] = "";
	size_t used = 0;
	for (unsigned i = 0; i < NOTE_CHUNKS; i++) {
		snprintf(key, sizeof(key), "a_%s_%d_%u", hash, index, i);
		const char *part = config_store_get(config_ebook_store(), SECTION, key, "");
		size_t n = strlen(part);
		if (n > 2 * NOTE_CHUNK || used + n >= sizeof(note) || (n && i && used != i * 2 * NOTE_CHUNK)) {
			note[0] = 0; break;
		}
		memcpy(note + used, part, n + 1); used += n;
	}
	if (!decode(note, out->note, sizeof(out->note)) ||
		!valid_text(out->note, sizeof(out->note), true)) out->note[0] = 0;
	return true;
}

int bookmark_find(const char *path, uint32_t spine, uint32_t block, uint32_t offset) {
	int count = bookmark_count(path);
	for (int i = 0; i < count; i++) {
		bookmark_t mark;
		if (bookmark_get(path, i, &mark) && mark.spine == spine && mark.block == block && mark.offset == offset) return i;
	}
	return -1;
}

typedef struct { char key[32], value[128]; } slot_t;

/* Rebuild only this book's keys in a single transaction. Even a completely
 * annotated 64-mark book uses at most 449 changes. Temporary storage is
 * bounded and freed before returning; there is no resident annotation cache. */
static bookmark_result_t write_book(const char *path, const bookmark_t *marks, int count, int previous) {
	int extent = count > previous ? count : previous;
	unsigned total = 1u + (unsigned)extent * KEYS_PER_MARK;
	slot_t *slots = calloc(total, sizeof(*slots));
	config_change_t *changes = calloc(total, sizeof(*changes));
	if (!slots || !changes) { free(slots); free(changes); return BOOKMARK_SAVE_FAILED; }
	char hash[17];
	bookmark_key(path, hash, sizeof(hash));
	count_key(path, slots[0].key, sizeof(slots[0].key));
	snprintf(slots[0].value, sizeof(slots[0].value), "%d", count);
	changes[0] = (config_change_t){slots[0].key, slots[0].value};
	unsigned at = 1;
	for (int i = 0; i < extent; i++) {
		slot_t *s = &slots[at];
		entry_key(path, i, s->key, sizeof(s->key));
		if (i < count) {
			int n = snprintf(s->value, sizeof(s->value), "%u:%u:%u|", marks[i].spine, marks[i].block, marks[i].offset);
			if (n < 0 || (size_t)n >= sizeof(s->value)) { free(slots); free(changes); return BOOKMARK_INVALID; }
			snippet(s->value + n, sizeof(s->value) - (size_t)n, marks[i].text);
		}
		changes[at++] = (config_change_t){s->key, i < count ? s->value : NULL};
		s = &slots[at];
		snprintf(s->key, sizeof(s->key), "p_%s_%d", hash, i);
		if (i < count && (marks[i].name[0] || marks[i].note[0]))
			snprintf(s->value, sizeof(s->value), "%u:%u:%u", marks[i].spine, marks[i].block, marks[i].offset);
		changes[at++] = (config_change_t){s->key, s->value[0] ? s->value : NULL};
		s = &slots[at];
		snprintf(s->key, sizeof(s->key), "t_%s_%d", hash, i);
		if (i < count) encode(marks[i].name, strlen(marks[i].name), s->value);
		changes[at++] = (config_change_t){s->key, s->value[0] ? s->value : NULL};
		size_t note_len = i < count ? strlen(marks[i].note) : 0;
		for (unsigned j = 0; j < NOTE_CHUNKS; j++) {
			s = &slots[at];
			snprintf(s->key, sizeof(s->key), "a_%s_%d_%u", hash, i, j);
			size_t start = j * NOTE_CHUNK;
			if (start < note_len) {
				size_t n = note_len - start;
				encode(marks[i].note + start, n < NOTE_CHUNK ? n : NOTE_CHUNK, s->value);
			}
			changes[at++] = (config_change_t){s->key, s->value[0] ? s->value : NULL};
		}
	}
	config_apply_result_t result = config_store_apply(config_ebook_store(), SECTION, changes, total);
	free(slots); free(changes);
	return result == CONFIG_APPLY_OK ? BOOKMARK_OK :
		result == CONFIG_APPLY_FULL ? BOOKMARK_STORAGE_FULL : BOOKMARK_SAVE_FAILED;
}

bookmark_result_t bookmark_save(const char *path, int index, const bookmark_t *mark) {
	if (!path || !path[0] || !mark || !memchr(mark->text, 0, sizeof(mark->text)) ||
		!valid_text(mark->name, sizeof(mark->name), false) || !valid_text(mark->note, sizeof(mark->note), true))
		return BOOKMARK_INVALID;
	int count = bookmark_count(path);
	if (index < -1 || index >= count) return BOOKMARK_INVALID;
	if (index == -1) {
		if (bookmark_find(path, mark->spine, mark->block, mark->offset) >= 0) return BOOKMARK_DUPLICATE;
		if (count == BOOKMARK_MAX_PER_BOOK) return BOOKMARK_FULL;
	}
	bookmark_t *marks = calloc(BOOKMARK_MAX_PER_BOOK, sizeof(*marks));
	if (!marks) return BOOKMARK_SAVE_FAILED;
	for (int i = 0; i < count; i++) {
		if (!bookmark_get(path, i, &marks[i])) { free(marks); return BOOKMARK_INVALID; }
	}
	if (index >= 0 && (marks[index].spine != mark->spine || marks[index].block != mark->block ||
		marks[index].offset != mark->offset)) { free(marks); return BOOKMARK_INVALID; }
	marks[index < 0 ? count : index] = *mark;
	bookmark_result_t result = write_book(path, marks, count + (index < 0), count);
	free(marks);
	return result;
}

bool bookmark_add(const char *path, uint32_t spine, uint32_t block, uint32_t offset, const char *text) {
	bookmark_t mark = {.spine = spine, .block = block, .offset = offset};
	snippet(mark.text, sizeof(mark.text), text ? text : "");
	return bookmark_save(path, -1, &mark) == BOOKMARK_OK;
}

bool bookmark_remove(const char *path, int index) {
	int count = bookmark_count(path);
	if (index < 0 || index >= count) return false;
	bookmark_t *marks = calloc(BOOKMARK_MAX_PER_BOOK, sizeof(*marks));
	if (!marks) return false;
	int at = 0;
	for (int i = 0; i < count; i++) {
		if (i != index && !bookmark_get(path, i, &marks[at++])) { free(marks); return false; }
	}
	bookmark_result_t result = write_book(path, marks, count - 1, count);
	free(marks);
	return result == BOOKMARK_OK;
}
