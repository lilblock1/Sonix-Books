#ifndef EBOOKFONTLIST_H
#define EBOOKFONTLIST_H

#include <stdbool.h>
#include <stddef.h>

#define EBOOKFONTLIST_MAX 32
#define EBOOKFONTLIST_ID 120
#define EBOOKFONTLIST_PATH 800
#define EBOOKFONTLIST_MAX_BYTES (16u * 1024u * 1024u)

typedef struct { char id[EBOOKFONTLIST_ID], name[EBOOKFONTLIST_ID]; } ebookfontlist_entry_t;
typedef struct {
	ebookfontlist_entry_t entries[EBOOKFONTLIST_MAX];
	int count;
	unsigned skipped;
	bool unavailable, truncated;
} ebookfontlist_t;

/* Flat .ttf/.otf files, or family/Regular.ttf (or Regular.otf), below
 * SD/Ebook/Fonts. Family folders may add Italic, Bold and BoldItalic files.
 * No font is rasterized here. Symlinks, traversal and oversized/bad containers
 * are rejected. Config IDs are relative and fit the existing settings store. */
void ebookfontlist_scan(const char *sd_root, ebookfontlist_t *out);
bool ebookfontlist_valid_id(const char *id);
bool ebookfontlist_resolve(const char *sd_root, const char *id, unsigned style, char *out, size_t size);
void ebookfontlist_name(const char *id, char *out, size_t size);

#endif
