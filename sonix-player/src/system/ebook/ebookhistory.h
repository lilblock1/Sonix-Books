#ifndef EBOOKHISTORY_H
#define EBOOKHISTORY_H

#include <stdbool.h>
#include <stddef.h>

// Stores only the existing bookmark path hash, so long filenames do not get
// truncated by the settings store. Reading positions keep their original keys.
void ebookhistory_remember(const char *path);
bool ebookhistory_is_last(const char *path);

// Resolves that hash against regular EPUB files currently on this card. No
// book is parsed and no remembered path from a different card is opened.
bool ebookhistory_find_last(const char *folder, char *out, size_t out_size);

#endif
