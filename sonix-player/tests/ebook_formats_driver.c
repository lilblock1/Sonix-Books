#include "src/system/ebook/ebook.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
	assert(argc == 2);
	ebook_t *book = ebook_open(argv[1]);
	if (!book) { fprintf(stderr, "%s\n", ebook_last_error()); return 1; }
	assert(!strcmp(ebook_path(book), argv[1]));
	assert(ebook_spine_count(book) > 0 && ebook_spine_count(book) <= 4096);
	uint64_t weight = 0;
	for (uint32_t i = 0; i < ebook_spine_count(book); i++) {
		weight += ebook_chapter_weight(book, i);
		assert(ebook_open_search_chapter(book, i));
		assert(ebook_chapter_bytes(book) < 4 * 1024 * 1024);
		const char *pool = ebook_chapter_text(book);
		for (uint32_t b = 0; b < ebook_block_count(book); b++) {
			const ebook_block_t *block = ebook_block(book, b);
			if (block->type == EBOOK_BLOCK_IMAGE || block->type == EBOOK_BLOCK_RULE) continue;
			for (uint32_t j = 0; j < block->span_count; j++) {
				const ebook_span_t *span = ebook_span(book, block->first_span + j);
				assert(fwrite(pool + span->offset, 1, span->length, stdout) == span->length);
			}
			fputc('\n', stdout);
		}
		ebook_close_chapter(book);
		assert(ebook_chapter_bytes(book) == 0);
	}
	assert(weight == ebook_book_weight(book));
	ebook_close(book);
	return 0;
}
