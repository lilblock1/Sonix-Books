#define _POSIX_C_SOURCE 200809L
#include <mupdf/fitz.h>
#include <mobi.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/system/ebook/booktool_protocol.h"

// A fresh process for each request returns all parser/font/codec allocations
// to the kernel. MuPDF allocations have an additional live-byte ceiling.
#define HEAP_MAX (16u * 1024u * 1024u)
typedef union { max_align_t alignment; struct { size_t size; } value; } allocation_t;
static size_t allocated;
static void *limited_malloc(void *user, size_t size) {
	(void)user;
	if (size > HEAP_MAX - allocated || size > SIZE_MAX - sizeof(allocation_t)) return NULL;
	allocation_t *p = malloc(sizeof(*p) + size);
	if (!p) return NULL;
	p->value.size = size; allocated += size; return p + 1;
}
static void limited_free(void *user, void *memory) {
	(void)user;
	if (!memory) return;
	allocation_t *p = (allocation_t *)memory - 1;
	allocated -= p->value.size; free(p);
}
static void *limited_realloc(void *user, void *memory, size_t size) {
	if (!memory) return limited_malloc(user, size);
	if (!size) { limited_free(user, memory); return NULL; }
	allocation_t *p = (allocation_t *)memory - 1;
	size_t before = p->value.size;
	if (size > HEAP_MAX - (allocated - before) || size > SIZE_MAX - sizeof(*p)) return NULL;
	allocation_t *next = realloc(p, sizeof(*p) + size);
	if (!next) return NULL;
	next->value.size = size; allocated = allocated - before + size; return next + 1;
}

static bool suffix(const char *path, const char *ext) {
	size_t a = strlen(path), b = strlen(ext);
	return a > b && !strcasecmp(path + a - b, ext);
}
static void append(fz_context *ctx, fz_buffer *out, const char *text) {
	if (fz_buffer_storage(ctx, out, NULL) + strlen(text) > 4u * 1024u * 1024u)
		fz_throw(ctx, FZ_ERROR_LIMIT, "page text limit");
	fz_append_string(ctx, out, text);
}
static void rune(fz_context *ctx, fz_buffer *out, int c) {
	if (c == '&') append(ctx, out, "&amp;");
	else if (c == '<') append(ctx, out, "&lt;");
	else if (c == '>') append(ctx, out, "&gt;");
	else if (c == '"') append(ctx, out, "&quot;");
	else {
		char bytes[8]; int n = fz_runetochar(bytes, c);
		if (fz_buffer_storage(ctx, out, NULL) + (size_t)n > 4u * 1024u * 1024u)
			fz_throw(ctx, FZ_ERROR_LIMIT, "page text limit");
		fz_append_data(ctx, out, bytes, (size_t)n);
	}
}

static fz_buffer *page_text(fz_context *ctx, fz_page *page) {
	fz_stext_page *text = NULL;
	fz_buffer *out = NULL;
	fz_var(text); fz_var(out);
	fz_try(ctx) {
		fz_stext_options options = {0};
		text = fz_new_stext_page_from_page(ctx, page, &options);
		out = fz_new_buffer(ctx, 4096);
		append(ctx, out, "<html><body>");
		for (fz_stext_block *b = text->first_block; b; b = b->next) {
			if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
			append(ctx, out, "<p>");
			for (fz_stext_line *line = b->u.t.first_line; line; line = line->next) {
				int style = 0;
				for (fz_stext_char *ch = line->first_char; ch; ch = ch->next) {
					int next = (fz_font_is_bold(ctx, ch->font) ? 1 : 0) | (fz_font_is_italic(ctx, ch->font) ? 2 : 0);
					if (style != next) {
						if (style & 2) append(ctx, out, "</i>");
						if (style & 1) append(ctx, out, "</b>");
						if (next & 1) append(ctx, out, "<b>");
						if (next & 2) append(ctx, out, "<i>");
						style = next;
					}
					if (ch->c >= 32 || ch->c == '\t') rune(ctx, out, ch->c);
				}
				if (style & 2) append(ctx, out, "</i>");
				if (style & 1) append(ctx, out, "</b>");
				if (line->next) append(ctx, out, " ");
			}
			append(ctx, out, "</p>");
		}
		append(ctx, out, "</body></html>");
		if (fz_buffer_storage(ctx, out, NULL) > BOOKTOOL_TEXT_MAX) fz_throw(ctx, FZ_ERROR_LIMIT, "page text limit");
	}
	fz_always(ctx) fz_drop_stext_page(ctx, text);
	fz_catch(ctx) { fz_drop_buffer(ctx, out); fz_rethrow(ctx); }
	return out;
}

static const uint16_t cp1252[32] = {
	0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
	0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
};
typedef struct { bool skip; int uc; } rtf_state_t;
static fz_buffer *rtf_html(fz_context *ctx, const char *path) {
	fz_buffer *input = NULL, *out = NULL;
	fz_var(input); fz_var(out);
	fz_try(ctx) {
		input = fz_read_file(ctx, path);
		unsigned char *data; size_t size = fz_buffer_storage(ctx, input, &data);
		if (size > 4u * 1024u * 1024u || size < 5 || memcmp(data, "{\\rtf", 5)) fz_throw(ctx, FZ_ERROR_FORMAT, "not RTF");
		out = fz_new_buffer(ctx, 4096);
		append(ctx, out, "<html><body><p>");
		rtf_state_t state = {false, 1}, stack[64]; unsigned depth = 0;
		int skip = 0, high = 0;
		for (size_t i = 0; i < size;) {
			unsigned c = data[i++];
			if (c == '{') { if (depth == 64) fz_throw(ctx, FZ_ERROR_LIMIT, "RTF depth"); stack[depth++] = state; continue; }
			if (c == '}') { if (!depth) fz_throw(ctx, FZ_ERROR_FORMAT, "RTF group"); state = stack[--depth]; continue; }
			if (c == '\\') {
				if (i == size) break;
				c = data[i++];
				if (c == '*') { state.skip = true; continue; }
				if (c == '\'' && i + 2 <= size) {
					char hex[3] = {(char)data[i], (char)data[i + 1], 0}, *end;
					c = (unsigned)strtoul(hex, &end, 16); if (*end) fz_throw(ctx, FZ_ERROR_FORMAT, "RTF hex"); i += 2;
					if (skip) { skip--; continue; }
					if (!state.skip) rune(ctx, out, c >= 128 && c < 160 ? cp1252[c - 128] : (int)c);
					continue;
				}
				if (c == '\\' || c == '{' || c == '}') { if (skip) skip--; else if (!state.skip) rune(ctx, out, (int)c); continue; }
				if (c == '~') { if (!state.skip) rune(ctx, out, ' '); continue; }
				if (c < 'a' || c > 'z') continue;
				char word[32]; size_t used = 0;
				do { if (used + 1 < sizeof(word)) word[used++] = (char)c; c = i < size ? data[i] : 0; if (c >= 'a' && c <= 'z') i++; else break; } while (true);
				word[used] = 0;
				int sign = 1, value = 0; if (i < size && data[i] == '-') { sign = -1; i++; }
				while (i < size && data[i] >= '0' && data[i] <= '9') { if (value > 1000000) fz_throw(ctx, FZ_ERROR_LIMIT, "RTF number"); value = value * 10 + data[i++] - '0'; }
				value *= sign; if (i < size && data[i] == ' ') i++;
				if (!strcmp(word, "fonttbl") || !strcmp(word, "colortbl") || !strcmp(word, "stylesheet") || !strcmp(word, "info") ||
					!strcmp(word, "pict") || !strcmp(word, "object") || !strcmp(word, "header") || !strcmp(word, "footer")) state.skip = true;
				if (!strcmp(word, "bin")) { if (value < 0 || (size_t)value > size - i) fz_throw(ctx, FZ_ERROR_FORMAT, "RTF binary"); i += (size_t)value; }
				if (state.skip) continue;
				if (!strcmp(word, "ansicpg") && value != 1252 && value != 0) fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "RTF code page");
				if (!strcmp(word, "uc")) { if (value < 0 || value > 16) fz_throw(ctx, FZ_ERROR_LIMIT, "RTF fallback"); state.uc = value; }
				if (!strcmp(word, "u")) {
					unsigned u = (unsigned)(uint16_t)value;
					if (u >= 0xd800 && u <= 0xdbff) high = (int)u;
					else {
						if (high && u >= 0xdc00 && u <= 0xdfff) u = 0x10000u + ((unsigned)high - 0xd800u) * 1024u + u - 0xdc00u;
						else if (high) rune(ctx, out, 0xfffd);
						high = 0;
						rune(ctx, out, u >= 0xd800 && u <= 0xdfff ? 0xfffd : (int)u);
					}
					skip = state.uc;
				} else if (!strcmp(word, "par") || !strcmp(word, "line")) append(ctx, out, "</p><p>");
				else if (!strcmp(word, "tab")) append(ctx, out, " ");
				continue;
			}
			if (c == '\r' || c == '\n') continue;
			if (skip) { skip--; continue; }
			if (!state.skip) rune(ctx, out, c >= 128 && c < 160 ? cp1252[c - 128] : (int)c);
		}
		if (depth) fz_throw(ctx, FZ_ERROR_FORMAT, "unclosed RTF group");
		append(ctx, out, "</p></body></html>");
	}
	fz_always(ctx) fz_drop_buffer(ctx, input);
	fz_catch(ctx) { fz_drop_buffer(ctx, out); fz_rethrow(ctx); }
	return out;
}

static void odt_nodes(fz_context *ctx, fz_buffer *out, fz_xml *node, unsigned depth) {
	if (depth > 64) fz_throw(ctx, FZ_ERROR_LIMIT, "ODT XML depth");
	for (; node; node = fz_xml_next(node)) {
		const char *text = fz_xml_text(node), *tag = fz_xml_tag(node);
		if (text) { while (*text) { int c; text += fz_chartorune(&c, text); rune(ctx, out, c); } continue; }
		bool paragraph = tag && (!strcmp(tag, "p") || !strcmp(tag, "h"));
		if (paragraph) append(ctx, out, "<p>");
		if (tag && (!strcmp(tag, "line-break") || !strcmp(tag, "tab"))) append(ctx, out, " ");
		odt_nodes(ctx, out, fz_xml_down(node), depth + 1);
		if (paragraph) append(ctx, out, "</p>");
	}
}
static fz_buffer *odt_html(fz_context *ctx, const char *path) {
	fz_archive *archive = NULL; fz_buffer *xml = NULL, *out = NULL; fz_xml *tree = NULL;
	fz_var(archive); fz_var(xml); fz_var(out); fz_var(tree);
	fz_try(ctx) {
		archive = fz_open_archive(ctx, path);
		xml = fz_read_archive_entry(ctx, archive, "content.xml");
		if (fz_buffer_storage(ctx, xml, NULL) > 4u * 1024u * 1024u) fz_throw(ctx, FZ_ERROR_LIMIT, "ODT XML size");
		tree = fz_parse_xml(ctx, xml, 1);
		fz_xml *body = fz_xml_find_dfs(fz_xml_root(tree), "body", NULL, NULL);
		if (!body) fz_throw(ctx, FZ_ERROR_FORMAT, "ODT body missing");
		out = fz_new_buffer(ctx, 4096); append(ctx, out, "<html><body>");
		odt_nodes(ctx, out, fz_xml_down(body), 0); append(ctx, out, "</body></html>");
	}
	fz_always(ctx) { fz_drop_xml(ctx, tree); fz_drop_buffer(ctx, xml); fz_drop_archive(ctx, archive); }
	fz_catch(ctx) { fz_drop_buffer(ctx, out); fz_rethrow(ctx); }
	return out;
}

static int send_response(booktool_status_t status, unsigned pages, unsigned flags, const char *title, const char *author,
	const unsigned char *text, size_t text_size, const unsigned char *png, size_t png_size) {
	unsigned char header[BOOKTOOL_HEADER] = {0};
	memcpy(header, BOOKTOOL_MAGIC, 4); booktool_put32(header + 4, status);
	booktool_put32(header + 8, pages); booktool_put32(header + 12, flags);
	booktool_put32(header + 16, (uint32_t)text_size); booktool_put32(header + 20, (uint32_t)png_size);
	if (title) snprintf((char *)header + 24, 256, "%s", title);
	if (author) snprintf((char *)header + 280, 128, "%s", author);
	if (fwrite(header, 1, sizeof(header), stdout) != sizeof(header) ||
		(text_size && fwrite(text, 1, text_size, stdout) != text_size) ||
		(png_size && fwrite(png, 1, png_size, stdout) != png_size) || fflush(stdout)) return 1;
	return 0;
}

static bool directory(const char *path) {
	struct stat st;
	if (!lstat(path, &st)) return S_ISDIR(st.st_mode);
	return errno == ENOENT && !mkdir(path, 0755);
}
static bool bounded_regular(const char *path) {
	struct stat st;
	return !lstat(path, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && (uint64_t)st.st_size <= BOOKTOOL_FILE_MAX;
}
static int prepare_kindle(const char *tool, const char *path, const char *root) {
	char managed[1200], cache[1200], final[1400], stage[1400], converter[1400];
	if (!root || !root[0] || strlen(root) > 800 || !directory(root)) goto fail;
	int n = snprintf(managed, sizeof(managed), "%s/.sonix", root);
	if (n < 0 || (size_t)n >= sizeof(managed) || !directory(managed)) goto fail;
	n = snprintf(cache, sizeof(cache), "%s/book-cache", managed);
	if (n < 0 || (size_t)n >= sizeof(cache) || !directory(cache)) goto fail;
	FILE *input = fopen(path, "rb"); if (!input) goto fail;
	fz_sha256 hash; fz_sha256_init(&hash);
	unsigned char chunk[65536], digest[32]; size_t got;
	while ((got = fread(chunk, 1, sizeof(chunk), input))) fz_sha256_update(&hash, chunk, got);
	bool good = !ferror(input); fclose(input); if (!good) goto fail;
	fz_sha256_final(&hash, digest);
	char key[65]; for (unsigned i = 0; i < 32; i++) snprintf(key + i * 2, 3, "%02x", digest[i]);
	n = snprintf(final, sizeof(final), "%s/%s.epub", cache, key);
	if (n < 0 || (size_t)n >= sizeof(final)) goto fail;
	if (bounded_regular(final)) return send_response(BOOKTOOL_OK, 0, BOOKTOOL_EPUB, NULL, NULL, (const unsigned char *)final, strlen(final), NULL, 0);
	MOBIData *mobi = mobi_init(); if (!mobi) goto fail;
	MOBI_RET result = mobi_load_filename(mobi, path);
	bool protected = mobi_is_encrypted(mobi), replica = mobi_is_replica(mobi);
	mobi_free(mobi);
	if (protected || result == MOBI_FILE_ENCRYPTED) return send_response(BOOKTOOL_PROTECTED, 0, 0, NULL, NULL, NULL, 0, NULL, 0);
	if (result != MOBI_SUCCESS || replica) goto fail;
	struct statvfs space;
	if (statvfs(cache, &space) || (uint64_t)space.f_bavail * (space.f_frsize ? space.f_frsize : space.f_bsize) < BOOKTOOL_FILE_MAX + 8u * 1024u * 1024u) goto fail;
	n = snprintf(stage, sizeof(stage), "%s/import-XXXXXX", cache);
	if (n < 0 || (size_t)n >= sizeof(stage) || !mkdtemp(stage)) goto fail;
	const char *slash = strrchr(tool, '/');
	n = snprintf(converter, sizeof(converter), "%.*s/mobitool", slash ? (int)(slash - tool) : 1, slash ? tool : ".");
	bool converted = false;
	if (n > 0 && (size_t)n < sizeof(converter)) {
		pid_t child = fork();
		if (!child) {
			int null = open("/dev/null", O_WRONLY);
			if (null >= 0) { (void)dup2(null, STDOUT_FILENO); (void)dup2(null, STDERR_FILENO); close(null); }
			execl(converter, converter, "-e", "-o", stage, path, (char *)NULL); _exit(127);
		}
		if (child > 0) {
			int status = 0; pid_t waited;
			do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
			converted = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
		}
	}
	DIR *dir = opendir(stage); struct dirent *entry;
	unsigned found = 0; char candidate[1800] = "";
	if (dir) {
		while ((entry = readdir(dir))) {
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
			char file[1800]; n = snprintf(file, sizeof(file), "%s/%s", stage, entry->d_name);
			if (n < 0 || (size_t)n >= sizeof(file)) { converted = false; continue; }
			if (converted && suffix(file, ".epub") && bounded_regular(file)) { found++; memcpy(candidate, file, strlen(file) + 1); }
		}
		closedir(dir);
	}
	converted = converted && found == 1;
	if (converted) {
		int fd = open(candidate, O_RDONLY | O_NOFOLLOW);
		converted = fd >= 0 && !fsync(fd); if (fd >= 0) close(fd);
		if (converted) converted = !rename(candidate, final);
	}
	dir = opendir(stage);
	if (dir) {
		while ((entry = readdir(dir))) {
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
			char file[1800]; n = snprintf(file, sizeof(file), "%s/%s", stage, entry->d_name);
			if (n > 0 && (size_t)n < sizeof(file)) unlink(file);
		}
		closedir(dir);
	}
	rmdir(stage);
	if (converted) return send_response(BOOKTOOL_OK, 0, BOOKTOOL_EPUB, NULL, NULL, (const unsigned char *)final, strlen(final), NULL, 0);
fail:
	return send_response(BOOKTOOL_BAD_FILE, 0, 0, NULL, NULL, NULL, 0, NULL, 0);
}

int main(int argc, char **argv) {
	if (argc != 4 && argc != 5) return send_response(BOOKTOOL_BAD_FILE, 0, 0, NULL, NULL, NULL, 0, NULL, 0);
	// The launcher also enforces a wall-clock deadline. These are independent
	// ceilings on expensive/invalid files, not changes to the audio process.
	struct rlimit memory = {64u * 1024u * 1024u, 64u * 1024u * 1024u}, cpu = {20, 20}, output = {128u * 1024u * 1024u, 128u * 1024u * 1024u};
	setrlimit(RLIMIT_AS, &memory); setrlimit(RLIMIT_CPU, &cpu); setrlimit(RLIMIT_FSIZE, &output);
	struct stat st;
	if (stat(argv[3], &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOKTOOL_FILE_MAX)
		return send_response(BOOKTOOL_LIMIT, 0, 0, NULL, NULL, NULL, 0, NULL, 0);
	if (!strcmp(argv[1], "prepare")) return prepare_kindle(argv[0], argv[3], argc == 5 ? argv[4] : NULL);
	if (strcmp(argv[1], "info") && strcmp(argv[1], "text") && strcmp(argv[1], "page"))
		return send_response(BOOKTOOL_BAD_FILE, 0, 0, NULL, NULL, NULL, 0, NULL, 0);
	fz_alloc_context alloc = {NULL, limited_malloc, limited_realloc, limited_free};
	fz_context *ctx = fz_new_context(&alloc, NULL, 4u * 1024u * 1024u);
	if (!ctx) return send_response(BOOKTOOL_LIMIT, 0, 0, NULL, NULL, NULL, 0, NULL, 0);
	fz_document *doc = NULL; fz_page *page = NULL; fz_buffer *source = NULL, *text = NULL, *png = NULL; fz_pixmap *pixmap = NULL;
	booktool_status_t status = BOOKTOOL_OK; unsigned pages = 0, flags = 0;
	char title[256] = "", author[128] = "";
	fz_var(doc); fz_var(page); fz_var(source); fz_var(text); fz_var(png); fz_var(pixmap); fz_var(status); fz_var(pages); fz_var(flags);
	fz_try(ctx) {
		fz_register_document_handlers(ctx);
		if (suffix(argv[3], ".rtf")) source = rtf_html(ctx, argv[3]);
		if (suffix(argv[3], ".odt")) source = odt_html(ctx, argv[3]);
		if (suffix(argv[3], ".fbz")) {
			fz_archive *archive = fz_open_archive(ctx, argv[3]);
			fz_try(ctx) {
				int entries = fz_count_archive_entries(ctx, archive);
				if (entries > 1024) fz_throw(ctx, FZ_ERROR_LIMIT, "FBZ entries");
				for (int i = 0; i < entries && !source; i++) {
					const char *name = fz_list_archive_entry(ctx, archive, i);
					if (suffix(name, ".fb2")) source = fz_read_archive_entry(ctx, archive, name);
				}
				if (!source) fz_throw(ctx, FZ_ERROR_FORMAT, "FB2 missing");
			}
			fz_always(ctx) fz_drop_archive(ctx, archive);
			fz_catch(ctx) fz_rethrow(ctx);
		}
		doc = source ? fz_open_document_with_buffer(ctx, suffix(argv[3], ".fbz") ? "book.fb2" : "text/html", source) : fz_open_document(ctx, argv[3]);
		if (fz_needs_password(ctx, doc)) { status = BOOKTOOL_PROTECTED; fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "protected book"); }
		bool reflow = fz_is_document_reflowable(ctx, doc);
		if (reflow) fz_layout_document(ctx, doc, 400, 600, 20);
		else flags |= BOOKTOOL_FIXED;
		int count = fz_count_pages(ctx, doc);
		if (count < 1 || (unsigned)count > BOOKTOOL_PAGES_MAX) fz_throw(ctx, FZ_ERROR_LIMIT, "page count limit");
		pages = (unsigned)count;
		fz_lookup_metadata(ctx, doc, FZ_META_INFO_TITLE, title, sizeof(title));
		fz_lookup_metadata(ctx, doc, FZ_META_INFO_AUTHOR, author, sizeof(author));
		if (strcmp(argv[1], "info")) {
			char *end; unsigned long index = strtoul(argv[2], &end, 10);
			if (!argv[2][0] || *end || index >= pages) fz_throw(ctx, FZ_ERROR_FORMAT, "page index");
			page = fz_load_page(ctx, doc, (int)index);
			if (!strcmp(argv[1], "text") || !(flags & BOOKTOOL_FIXED)) text = page_text(ctx, page);
			else if (!strcmp(argv[1], "page")) {
				fz_rect bounds = fz_bound_page(ctx, page);
				float width = bounds.x1 - bounds.x0, height = bounds.y1 - bounds.y0;
				if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) fz_throw(ctx, FZ_ERROR_FORMAT, "page dimensions");
				float scale = fminf(960 / width, 1280 / height);
				pixmap = fz_new_pixmap_from_page(ctx, page, fz_scale(scale, scale), fz_device_rgb(ctx), 0);
				png = fz_new_buffer_from_pixmap_as_png(ctx, pixmap, fz_default_color_params);
				if (fz_buffer_storage(ctx, png, NULL) > BOOKTOOL_IMAGE_MAX) fz_throw(ctx, FZ_ERROR_LIMIT, "page image size");
			} else fz_throw(ctx, FZ_ERROR_FORMAT, "unknown operation");
		}
	}
	fz_catch(ctx) {
		fprintf(stderr, "native reader: %s\n", fz_caught_message(ctx));
		if (status == BOOKTOOL_OK) status = fz_caught(ctx) == FZ_ERROR_SYSTEM || fz_caught(ctx) == FZ_ERROR_LIMIT ? BOOKTOOL_LIMIT : BOOKTOOL_BAD_FILE;
	}
	unsigned char *text_data = NULL, *png_data = NULL;
	size_t text_size = status == BOOKTOOL_OK && text ? fz_buffer_storage(ctx, text, &text_data) : 0;
	size_t png_size = status == BOOKTOOL_OK && png ? fz_buffer_storage(ctx, png, &png_data) : 0;
	int result = send_response(status, pages, flags, title, author, text_data, text_size, png_data, png_size);
	fz_drop_pixmap(ctx, pixmap); fz_drop_buffer(ctx, png); fz_drop_buffer(ctx, text); fz_drop_buffer(ctx, source);
	fz_drop_page(ctx, page); fz_drop_document(ctx, doc); fz_drop_context(ctx);
	return result;
}
