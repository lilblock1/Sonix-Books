#ifndef EBOOK_NATIVE_H
#define EBOOK_NATIVE_H
#include "epub_internal.h"
#include "booktool_protocol.h"

typedef struct {
	unsigned pages, flags;
	char title[256], author[128];
	char *text;
	unsigned char *png;
	uint32_t text_size, png_size;
} ebook_native_result_t;
bool ebook_native_call(const char *path, const char *operation, uint32_t page,
	epub_arena_t *arena, ebook_native_result_t *out);
void ebook_native_error(const char *message);
bool ebook_text_open(ebook_t *book);
bool ebook_text_chapter(ebook_t *book, uint32_t chapter);

#endif
