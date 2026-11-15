#include "ebooktext.h"

uint32_t ebooktext_decode(const char *text, size_t size, size_t *offset) {
	if (!text || !offset || *offset >= size) return 0;
	size_t at = *offset;
	uint32_t c = (unsigned char)text[at];
	*offset = at + 1;
	if (c < 128) return c;
	unsigned more;
	uint32_t minimum;
	if (c >= 0xc2 && c <= 0xdf) { c &= 31; more = 1; minimum = 0x80; }
	else if (c >= 0xe0 && c <= 0xef) { c &= 15; more = 2; minimum = 0x800; }
	else if (c >= 0xf0 && c <= 0xf4) { c &= 7; more = 3; minimum = 0x10000; }
	else return 0xfffd;
	if (size - at - 1 < more) return 0xfffd;
	for (unsigned i = 1; i <= more; i++) {
		unsigned char next = (unsigned char)text[at + i];
		if ((next & 0xc0) != 0x80) return 0xfffd;
		c = (c << 6) | (next & 63);
	}
	if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0xfffd;
	*offset = at + more + 1;
	return c;
}

uint32_t ebooktext_fold(uint32_t c) {
	if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7) ||
		(c >= 0x391 && c <= 0x3ab && c != 0x3a2) || (c >= 0x410 && c <= 0x42f)) c += 32;
	else if (c >= 0x400 && c <= 0x40f) c += 80;
	else if ((c >= 0x100 && c <= 0x12f && !(c & 1)) ||
			 (c >= 0x132 && c <= 0x137 && !(c & 1)) ||
			 (c >= 0x14a && c <= 0x177 && !(c & 1))) c++;
	else if (c == 0x178) c = 0xff;
	else if (c == 0x3c2) c = 0x3c3;
	return c;
}

uint32_t ebooktext_next_folded(const char **text) {
	if (!text || !*text || !**text) return 0;
	size_t size = 0, offset = 0;
	while (size < 4 && (*text)[size]) size++;
	uint32_t code = ebooktext_decode(*text, size, &offset);
	*text += offset;
	return ebooktext_fold(code);
}
