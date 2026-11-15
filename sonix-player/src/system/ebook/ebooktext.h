#ifndef EBOOKTEXT_H
#define EBOOKTEXT_H

#include <stddef.h>
#include <stdint.h>

/* Bounded UTF-8 decoding. Invalid input consumes one byte and yields U+FFFD. */
uint32_t ebooktext_decode(const char *text, size_t size, size_t *offset);
uint32_t ebooktext_fold(uint32_t code);
uint32_t ebooktext_next_folded(const char **text);

#endif
