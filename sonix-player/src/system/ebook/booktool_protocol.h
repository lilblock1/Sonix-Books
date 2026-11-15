#ifndef BOOKTOOL_PROTOCOL_H
#define BOOKTOOL_PROTOCOL_H

#include <stdint.h>

// Byte protocol between the player and the isolated native reader process.
// Always little-endian; do not send a compiler-dependent struct over the pipe.
#define BOOKTOOL_MAGIC "BK01"
#define BOOKTOOL_HEADER 408u
#define BOOKTOOL_TEXT_MAX (256u * 1024u)
#define BOOKTOOL_IMAGE_MAX (4u * 1024u * 1024u)
#define BOOKTOOL_FILE_MAX (128u * 1024u * 1024u)
#define BOOKTOOL_PAGES_MAX 4096u
#define BOOKTOOL_FIXED 1u
#define BOOKTOOL_EPUB 2u
typedef enum {
	BOOKTOOL_OK, BOOKTOOL_BAD_FILE, BOOKTOOL_PROTECTED, BOOKTOOL_LIMIT,
	BOOKTOOL_IO, BOOKTOOL_UNSUPPORTED, BOOKTOOL_CANCELLED, BOOKTOOL_MISSING
} booktool_status_t;

static inline uint32_t booktool_u32(const unsigned char *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void booktool_put32(unsigned char *p, uint32_t n) {
	p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8);
	p[2] = (unsigned char)(n >> 16); p[3] = (unsigned char)(n >> 24);
}

#endif
