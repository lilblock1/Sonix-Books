#include "ebooksearch.h"
#include "ebooktext.h"

#include <string.h>

static uint32_t normalized(uint32_t c) {
	if ((c >= 9 && c <= 13) || c == ' ' || c == 0xa0 || (c >= 0x2000 && c <= 0x200a) ||
		c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x3000) return ' ';
	return ebooktext_fold(c);
}

static bool cancelled(ebooksearch_result_t *out, ebooksearch_cancel_cb cancel, void *user) {
	if (cancel && cancel(user)) out->cancelled = true;
	return out->cancelled;
}

/* Copy only the needed slice of the block, skipping entire spans without
 * flattening a large paragraph. No snippet points into a chapter arena. */
static void snippet(ebook_t *book, const ebook_block_t *block, uint32_t match, char *out, size_t size) {
	char raw[EBOOKSEARCH_SNIPPET];
	size_t used = 0;
	bool more = false;
	uint64_t offset = 0, start = match > 40 ? match - 40 : 0, total = 0;
	const char *text = ebook_chapter_text(book);
	for (uint32_t i = 0; i < block->span_count; i++) {
		const ebook_span_t *span = ebook_span(book, block->first_span + i);
		if (!span) continue;
		total += span->length;
		if (used < sizeof(raw) && offset + span->length > start) {
			size_t skip = start > offset ? (size_t)(start - offset) : 0;
			size_t n = span->length - skip;
			if (n > sizeof(raw) - used) n = sizeof(raw) - used;
			memcpy(raw + used, text + span->offset + skip, n);
			used += n;
		}
		offset += span->length;
		if (used == sizeof(raw)) { more = i + 1 < block->span_count; break; }
	}
	size_t at = 0, written = 0;
	if (start) { memcpy(out, "... ", 4); written = 4; }
	while (at < used && ((unsigned char)raw[at] & 0xc0) == 0x80) at++;
	bool space = false;
	while (at < used) {
		size_t before = at;
		uint32_t c = ebooktext_decode(raw, used, &at);
		if (normalized(c) == ' ' || c < 32) {
			if (space) continue;
			if (written + 5 >= size) break;
			out[written++] = ' ';
			space = true;
		} else {
			size_t n = at - before;
			if (written + n + 5 >= size) break;
			if (c == 0xfffd && n == 1) out[written++] = '?';
			else { memcpy(out + written, raw + before, n); written += n; }
			space = false;
		}
	}
	if ((more || start + at < total) && written + 3 < size) { memcpy(out + written, "...", 3); written += 3; }
	out[written] = 0;
}

void ebooksearch_run(const char *path, const char *query, ebooksearch_result_t *out,
					 ebooksearch_cancel_cb cancel, ebooksearch_progress_cb progress, void *user) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	uint32_t pattern[EBOOKSEARCH_QUERY_CHARS], offsets[EBOOKSEARCH_QUERY_CHARS];
	uint16_t prefix[EBOOKSEARCH_QUERY_CHARS] = {0};
	size_t bytes = 0;
	if (query) while (bytes < EBOOKSEARCH_QUERY_BYTES && query[bytes]) bytes++;
	if (bytes == EBOOKSEARCH_QUERY_BYTES) { out->status = EBOOKSEARCH_LONG_QUERY; return; }
	unsigned count = 0;
	for (size_t at = 0; at < bytes;) {
		uint32_t c = normalized(ebooktext_decode(query, bytes, &at));
		if (c == ' ' && (!count || pattern[count - 1] == ' ')) continue;
		if (count == EBOOKSEARCH_QUERY_CHARS) { out->status = EBOOKSEARCH_LONG_QUERY; return; }
		pattern[count++] = c;
	}
	if (count && pattern[count - 1] == ' ') count--;
	if (!count) { out->status = EBOOKSEARCH_EMPTY_QUERY; return; }
	for (unsigned i = 1, j = 0; i < count; i++) {
		while (j && pattern[i] != pattern[j]) j = prefix[j - 1];
		if (pattern[i] == pattern[j]) j++;
		prefix[i] = (uint16_t)j;
	}
	if (cancelled(out, cancel, user)) return;
	ebook_t *book = ebook_open(path);
	if (!book) { out->status = EBOOKSEARCH_OPEN_FAILED; return; }
	out->chapters_total = ebook_spine_count(book);
	uint64_t chapter_bytes = 0, text_bytes = 0;
	unsigned checkpoint = 0;
	for (uint32_t chapter = 0; chapter < out->chapters_total; chapter++) {
		if (cancelled(out, cancel, user)) break;
		uint32_t weight = ebook_chapter_weight(book, chapter);
		if (weight > EBOOKSEARCH_MAX_BYTES - chapter_bytes) { out->truncated = true; break; }
		chapter_bytes += weight;
		if (progress) progress(chapter + 1, out->chapters_total, user);
		out->chapters_scanned++;
		if (!ebook_open_chapter(book, chapter)) { out->unreadable++; continue; }
		const char *text = ebook_chapter_text(book);
		for (uint32_t b = 0; b < ebook_block_count(book); b++) {
			if (cancelled(out, cancel, user)) goto done;
			const ebook_block_t *block = ebook_block(book, b);
			if (!block || block->type == EBOOK_BLOCK_IMAGE || block->type == EBOOK_BLOCK_RULE) continue;
			unsigned matched = 0, ring = 0;
			uint64_t block_offset = 0;
			bool space = false;
			for (uint32_t s = 0; s < block->span_count; s++) {
				if (cancelled(out, cancel, user)) goto done;
				const ebook_span_t *span = ebook_span(book, block->first_span + s);
				if (!span) continue;
				for (size_t at = 0; at < span->length;) {
					if ((checkpoint++ & 1023u) == 0 && cancelled(out, cancel, user)) goto done;
					size_t before = at;
					uint32_t c = normalized(ebooktext_decode(text + span->offset, span->length, &at));
					text_bytes += at - before;
					if (text_bytes > EBOOKSEARCH_MAX_BYTES || block_offset + at > UINT32_MAX) {
						out->truncated = true; goto done;
					}
					if (c == ' ' && space) continue;
					space = c == ' ';
					offsets[ring] = (uint32_t)(block_offset + before);
					if (++ring == count) ring = 0;
					while (matched && c != pattern[matched]) matched = prefix[matched - 1];
					if (c == pattern[matched]) matched++;
					if (matched == count) {
						if (out->count == EBOOKSEARCH_MAX_HITS) { out->truncated = true; goto done; }
						ebooksearch_hit_t *hit = &out->hits[out->count++];
						hit->position = (ebook_position_t){chapter, b, offsets[ring]};
						hit->match_bytes = (uint32_t)(block_offset + at) - hit->position.offset;
						snippet(book, block, hit->position.offset, hit->snippet, sizeof(hit->snippet));
						matched = prefix[matched - 1];
					}
				}
				block_offset += span->length;
			}
		}
	}
done:
	ebook_close(book);
}
