#include "ebook.h"
#include <string.h>
#include <strings.h>

ebook_format_t ebook_format(const char *path) {
	if (!path) return EBOOK_FORMAT_UNSUPPORTED;
	const char *ext = strrchr(path, '.');
	if (!ext || strchr(ext, '/') || strchr(ext, '\\')) return EBOOK_FORMAT_UNSUPPORTED;
	if (!strcasecmp(ext, ".epub")) return EBOOK_FORMAT_EPUB;
	if (!strcasecmp(ext, ".txt") || !strcasecmp(ext, ".text")) return EBOOK_FORMAT_TEXT;
	if (!strcasecmp(ext, ".mobi") || !strcasecmp(ext, ".azw") || !strcasecmp(ext, ".azw3") || !strcasecmp(ext, ".prc")) return EBOOK_FORMAT_KINDLE;
	static const char *const formats[] = {".pdf", ".xps", ".oxps", ".fb2", ".fbz", ".html", ".htm", ".xhtml",
		".md", ".markdown", ".rtf", ".docx", ".odt", ".cbz", ".cbr", ".cbt"};
	for (unsigned i = 0; i < sizeof(formats) / sizeof(*formats); i++)
		if (!strcasecmp(ext, formats[i])) return EBOOK_FORMAT_NATIVE;
	return EBOOK_FORMAT_UNSUPPORTED;
}
bool ebook_supported(const char *path) { return ebook_format(path) != EBOOK_FORMAT_UNSUPPORTED; }
