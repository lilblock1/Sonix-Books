#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "src/system/ebook/ebook.h"
#include "src/system/ebook/ebooksearch.h"

int main(int argc, char **argv) {
	assert(argc == 2);
	ebook_t *reader = ebook_open(argv[1]);
	assert(reader && ebook_open_chapter(reader, 0));
	const ebook_block_t *original = ebook_block(reader, 0);
	uint32_t original_count = ebook_block_count(reader);
	ebooksearch_result_t *out = malloc(sizeof(*out)); assert(out);
	ebooksearch_run(argv[1], "styled phrase & needle", out, NULL, NULL, NULL);
	assert(out->status == EBOOKSEARCH_OK && out->count == 1 && !out->truncated && !out->unreadable);
	assert(out->hits[0].position.spine == 0 && out->hits[0].position.offset == 2);
	assert(strstr(out->hits[0].snippet, "styled phrase & needle"));
	/* Searching through another handle cannot move or invalidate the reader. */
	assert(ebook_chapter_index(reader) == 0 && ebook_block_count(reader) == original_count);
	assert(ebook_block(reader, 0) == original);
	ebook_close_chapter(reader); /* the production overlay does this before scanning */
	ebooksearch_run(argv[1], "needle", out, NULL, NULL, NULL);
	assert(out->count == 3 && out->chapters_scanned == 2 && !out->unreadable);
	for (int i = 0; i < out->count; i++) {
		ebook_position_t p = out->hits[i].position;
		assert(ebook_open_chapter(reader, p.spine));
		const ebook_block_t *block = ebook_block(reader, p.block); assert(block);
		char flat[512]; size_t length = 0;
		for (uint32_t s = 0; s < block->span_count; s++) {
			const ebook_span_t *span = ebook_span(reader, block->first_span + s); assert(span);
			assert(length + span->length + 1 < sizeof(flat));
			memcpy(flat + length, ebook_chapter_text(reader) + span->offset, span->length);
			length += span->length;
		}
		flat[length] = 0;
		assert(p.offset + 6 <= length && !strncasecmp(flat + p.offset, "needle", 6));
	}
	ebook_close(reader); free(out);
	puts("real EPUB search: compressed XML, styled phrases, original-handle isolation and exact result jumps passed");
	return 0;
}
