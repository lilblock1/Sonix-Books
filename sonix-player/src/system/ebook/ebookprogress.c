#include "ebookprogress.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bookmarks.h"
#include "src/system/core/config.h"

static void position_key(const char *path, char *out, size_t size) {
	char hash[24];
	bookmark_key(path, hash, sizeof(hash));
	snprintf(out, size, "pos_%s", hash);
}

static bool read_uint(const char **text, uint32_t *value) {
	const char *p = *text;
	if (*p < '0' || *p > '9') return false;
	errno = 0;
	char *end;
	unsigned long long n = strtoull(p, &end, 10);
	if (errno || n > UINT32_MAX) return false;
	*value = (uint32_t)n;
	*text = end;
	return true;
}

ebookprogress_t ebookprogress_get(const char *path) {
	ebookprogress_t progress = {.state = EBOOKPROGRESS_NEW, .known = true};
	if (!path || !path[0]) return progress;
	char key[32];
	position_key(path, key, sizeof(key));
	const char *text = config_store_get(config_ebook_store(), "ebook", key, "");
	ebook_position_t position;
	if (!read_uint(&text, &position.spine) || *text++ != ':' ||
		!read_uint(&text, &position.block) || *text++ != ':' || !read_uint(&text, &position.offset)) return progress;
	progress.position = position;
	progress.state = EBOOKPROGRESS_READING;
	progress.known = false;
	/* Keep a valid legacy resume even when an optional extension is damaged. */
	if (strncmp(text, "|p1:", 4)) return progress;
	text += 4;
	uint32_t percent, finished;
	if (!read_uint(&text, &percent) || *text++ != ':' || !read_uint(&text, &finished) || *text ||
		percent > 100 || finished > 1 || (finished && percent != 100) || (!finished && percent == 100)) return progress;
	progress.percent = (int)percent;
	progress.known = true;
	progress.state = finished ? EBOOKPROGRESS_FINISHED : EBOOKPROGRESS_READING;
	return progress;
}

static int position_compare(ebook_position_t a, ebook_position_t b) {
	if (a.spine != b.spine) return a.spine < b.spine ? -1 : 1;
	if (a.block != b.block) return a.block < b.block ? -1 : 1;
	return (a.offset > b.offset) - (a.offset < b.offset);
}

ebookprogress_t ebookprogress_advance(ebookprogress_t previous, ebook_position_t position, int percent, bool finish) {
	bool finished = finish || (previous.state == EBOOKPROGRESS_FINISHED &&
		position_compare(position, previous.position) >= 0);
	if (percent < 0) percent = 0;
	if (percent > 99) percent = 99;
	return (ebookprogress_t){.position = position, .state = finished ? EBOOKPROGRESS_FINISHED : EBOOKPROGRESS_READING,
		.percent = finished ? 100 : percent, .known = true};
}

void ebookprogress_store(const char *path, const ebookprogress_t *progress) {
	if (!path || !path[0] || !progress || progress->state == EBOOKPROGRESS_NEW) return;
	char key[32], value[64];
	position_key(path, key, sizeof(key));
	bool finished = progress->state == EBOOKPROGRESS_FINISHED;
	int percent = finished ? 100 : progress->percent;
	if (percent < 0) percent = 0;
	if (percent > 99 && !finished) percent = 99;
	snprintf(value, sizeof(value), "%u:%u:%u|p1:%d:%u", progress->position.spine,
		progress->position.block, progress->position.offset, percent, finished ? 1u : 0u);
	config_store_set(config_ebook_store(), "ebook", key, value);
	/* The reader flushes once, at its existing position-save points. */
}

int ebookprogress_chapter_percent(uint32_t block, uint32_t blocks, uint32_t offset, uint64_t block_length) {
	if (!blocks) return 0;
	if (block >= blocks) return 100;
	uint64_t within = 0;
	if (block_length) {
		uint64_t used = offset < block_length ? offset : block_length;
		within = used * 100u / block_length;
	}
	return (int)(((uint64_t)block * 100u + within) / blocks);
}
