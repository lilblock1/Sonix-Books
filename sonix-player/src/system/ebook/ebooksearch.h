#ifndef EBOOKSEARCH_H
#define EBOOKSEARCH_H

#include "ebook.h"

#define EBOOKSEARCH_MAX_HITS 100
#define EBOOKSEARCH_SNIPPET 192
#define EBOOKSEARCH_QUERY_BYTES 256
#define EBOOKSEARCH_QUERY_CHARS 96
#define EBOOKSEARCH_MAX_BYTES (64u * 1024u * 1024u)

typedef struct {
	ebook_position_t position;
	uint32_t match_bytes;
	char snippet[EBOOKSEARCH_SNIPPET];
} ebooksearch_hit_t;

typedef enum {
	EBOOKSEARCH_OK,
	EBOOKSEARCH_EMPTY_QUERY,
	EBOOKSEARCH_LONG_QUERY,
	EBOOKSEARCH_OPEN_FAILED
} ebooksearch_status_t;

typedef struct {
	ebooksearch_hit_t hits[EBOOKSEARCH_MAX_HITS];
	int count;
	ebooksearch_status_t status;
	uint32_t chapters_scanned, chapters_total, unreadable;
	bool cancelled, truncated;
} ebooksearch_result_t;

typedef bool (*ebooksearch_cancel_cb)(void *user);
typedef void (*ebooksearch_progress_cb)(uint32_t chapter, uint32_t total, void *user);

/* Worker-thread operation, with its OWN book handle. Never changes the reader,
 * config, fonts or images. Searches literal text within each paragraph/heading/
 * list item across style spans; collapses whitespace and uses simple Unicode
 * case folding. Results use original block byte offsets for exact reader jumps.
 * Holds one parsed chapter, caps matches and examined chapter/text bytes, and
 * checks cancellation between chapters/spans and every 1024 decoded characters.
 */
void ebooksearch_run(const char *path, const char *query, ebooksearch_result_t *out,
					 ebooksearch_cancel_cb cancel, ebooksearch_progress_cb progress, void *user);

#endif
