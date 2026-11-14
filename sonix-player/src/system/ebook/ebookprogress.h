#ifndef EBOOKPROGRESS_H
#define EBOOKPROGRESS_H

#include "ebook.h"

typedef enum {
	EBOOKPROGRESS_NEW,
	EBOOKPROGRESS_READING,
	EBOOKPROGRESS_FINISHED
} ebookprogress_state_t;

typedef struct {
	ebook_position_t position;
	ebookprogress_state_t state;
	int percent;
	bool known; /* legacy positions remain readable, but have no percentage yet */
} ebookprogress_t;

/* UI thread, like the config store. Reuses pos_<existing path hash>; no extra
 * config entries or per-book files. Old readers still parse its position prefix.
 */
ebookprogress_t ebookprogress_get(const char *path);
void ebookprogress_store(const char *path, const ebookprogress_t *progress);

/* Pure state transition. A finished book stays finished at the same/later
 * position; going back to an earlier position returns it to Reading.
 * Only an explicit end action sets finish=true. Normal estimates cap at 99%.
 */
ebookprogress_t ebookprogress_advance(ebookprogress_t previous, ebook_position_t position,
									int percent, bool finish);

/* Estimate within the current chapter, including offsets in long paragraphs.
 * Empty chapters are 0%; block >= blocks means the end. No allocation/parser.
 */
int ebookprogress_chapter_percent(uint32_t block, uint32_t blocks, uint32_t offset, uint64_t block_length);

#endif
