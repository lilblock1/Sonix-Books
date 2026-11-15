#define _POSIX_C_SOURCE 200809L
#include "sdpackages.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "src/system/core/json.h"
#include "src/system/core/sha256.h"
#include "src/system/image/miniz/miniz_tinfl.h"

#define PACKAGE_FILES 1024
#define MANIFEST_MAX (512u * 1024u)
#define PATH_SIZE 1200
#define SPACE_MARGIN (8u * 1024u * 1024u)

typedef struct {
	char path[256], sha[65];
	uint32_t bytes, compressed;
} member_t;
typedef struct {
	FILE *file;
	uint64_t done, total;
	sdpackages_cancel_t cancel;
	sdpackages_progress_t progress;
	void *user;
} transfer_t;

static bool stopped(transfer_t *t) { return t->cancel && t->cancel(t->user); }
static bool hex_digest(const char *sha) {
	if (!sha || strlen(sha) != 64) return false;
	for (unsigned i = 0; i < 64; i++) if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f'))) return false;
	return true;
}
static bool directory(const char *path, bool create) {
	struct stat st;
	if (!lstat(path, &st)) return S_ISDIR(st.st_mode);
	return create && errno == ENOENT && !mkdir(path, 0755);
}
static bool join(char *out, size_t size, const char *a, const char *b) {
	int n = snprintf(out, size, "%s/%s", a, b);
	return n > 0 && (size_t)n < size;
}
static bool safe_path(const char *path) {
	if (!path || strlen(path) >= 256 || (strncmp(path, "Ebook/Fonts/", 12) &&
		strncmp(path, "Ebook/Dictionaries/", 19) && strncmp(path, "Ebook/Tools/", 12))) return false;
	const char *part = path; unsigned depth = 0;
	for (const char *p = path;; p++) {
		if (*p && ((unsigned char)*p < 32 || (unsigned char)*p > 126 || *p == '\\' || *p == ':')) return false;
		if (*p == '/' || !*p) {
			size_t n = (size_t)(p - part);
			if (!n || part[0] == '.' || ++depth > 16) return false;
			if (!*p) return true;
			part = p + 1;
		}
	}
}
static bool parents(char *path, const char *stage) {
	size_t prefix = strlen(stage);
	if (strncmp(path, stage, prefix) || path[prefix] != '/') return false;
	for (char *p = path + prefix + 1; *p; p++) if (*p == '/') {
		*p = 0; bool ok = directory(path, true); *p = '/';
		if (!ok) return false;
	}
	return true;
}

static bool manifest_string(const json_doc_t *doc, int object, const char *key, char *out, size_t size) {
	int token = json_get(doc, object, key);
	if (token < 0 || doc->toks[token].type != JSON_STRING) return false;
	int length = doc->toks[token].end - doc->toks[token].start;
	if (length < 0 || (size_t)length >= size) return false;
	const char *value = doc->text + doc->toks[token].start;
	for (int i = 0; i < length; i++) if ((unsigned char)value[i] < 32 || (unsigned char)value[i] > 126 || value[i] == '\\') return false;
	memcpy(out, value, (size_t)length); out[length] = 0; return true;
}
static long manifest_number(const json_doc_t *doc, int object, const char *key) {
	int token = json_get(doc, object, key);
	if (token < 0 || doc->toks[token].type != JSON_NUMBER) return -1;
	int length = doc->toks[token].end - doc->toks[token].start;
	if (length < 1 || length > 9) return -1;
	long value = 0;
	for (int i = doc->toks[token].start; i < doc->toks[token].end; i++) {
		char c = doc->text[i]; if (c < '0' || c > '9') return -1;
		value = value * 10 + c - '0';
	}
	return value;
}
// Only used on a freshly created staging directory, or an explicitly owned
// complete version below .sonix/packages. Symlinks are unlinked, never followed.
static bool remove_tree(const char *path, unsigned depth, unsigned *visits) {
	if (depth > 18 || ++*visits > PACKAGE_FILES * 3) return false;
	struct stat st;
	if (lstat(path, &st)) return errno == ENOENT;
	if (!S_ISDIR(st.st_mode)) return !unlink(path);
	DIR *dir = opendir(path); if (!dir) return false;
	struct dirent *de; bool ok = true;
	while (ok && (de = readdir(dir))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		char child[PATH_SIZE];
		ok = join(child, sizeof(child), path, de->d_name) && remove_tree(child, depth + 1, visits);
	}
	closedir(dir);
	return ok && !rmdir(path);
}
static bool write_text(const char *path, const char *text) {
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
	if (fd < 0) return false;
	size_t length = strlen(text), done = 0;
	while (done < length) {
		ssize_t n = write(fd, text + done, length - done);
		if (n <= 0) { close(fd); unlink(path); return false; }
		done += (size_t)n;
	}
	bool ok = !fsync(fd);
	if (close(fd)) ok = false;
	if (!ok) unlink(path);
	return ok;
}
static bool set_pointer(const char *base, const char *name, const char *version) {
	char part[PATH_SIZE], final[PATH_SIZE], value[SDPACKAGES_VERSION + 2];
	if (!join(final, sizeof(final), base, name)) return false;
	int n = snprintf(part, sizeof(part), "%s.part-%ld", final, (long)getpid());
	if (n < 0 || (size_t)n >= sizeof(part)) return false;
	snprintf(value, sizeof(value), "%s\n", version);
	if (!write_text(part, value)) return false;
	if (rename(part, final)) { unlink(part); return false; }
	int fd = open(base, O_RDONLY | O_DIRECTORY);
	if (fd >= 0) { (void)fsync(fd); close(fd); }
	return true;
}

// Keep one complete rollback generation. Never touch manually installed
// Ebook files, staging directories, or a directory without our completion mark.
static void prune(const char *base, const char *active, const char *previous) {
	DIR *dir = opendir(base); if (!dir) return;
	struct dirent *de; unsigned checked = 0;
	while ((de = readdir(dir)) && ++checked <= 256) {
		if (!sdpackages_valid_version(de->d_name) || !strcmp(de->d_name, active) || !strcmp(de->d_name, previous)) continue;
		char path[PATH_SIZE], marker[PATH_SIZE];
		if (!join(path, sizeof(path), base, de->d_name) || !directory(path, false) || !join(marker, sizeof(marker), path, ".complete")) continue;
		struct stat st;
		if (lstat(marker, &st) || !S_ISREG(st.st_mode) || st.st_size < 8 || st.st_size > 95) continue;
		FILE *f = fopen(marker, "r"); if (!f) continue;
		char line[96]; bool ours = fgets(line, sizeof(line), f) && !strncmp(line, "SDPKG1 ", 7); fclose(f);
		if (!ours) continue;
		unsigned visits = 0; (void)remove_tree(path, 0, &visits);
	}
	closedir(dir);
}

static sdpackages_result_t extract(transfer_t *t, const member_t *member, const char *stage) {
	char path[PATH_SIZE];
	if (!join(path, sizeof(path), stage, member->path) || !parents(path, stage)) return SDPACKAGES_IO;
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
	if (fd < 0) return SDPACKAGES_IO;
	unsigned char *input = malloc(65536), *output = malloc(TINFL_LZ_DICT_SIZE);
	tinfl_decompressor *inflater = malloc(sizeof(*inflater));
	if (!input || !output || !inflater) { free(input); free(output); free(inflater); close(fd); return SDPACKAGES_IO; }
	tinfl_init(inflater);
	uint32_t left = member->compressed, made = 0;
	size_t in_at = 0, available = 0, out_at = 0;
	sha256_ctx sha; sha256_init(&sha);
	sdpackages_result_t result = SDPACKAGES_OK;
	for (;;) {
		if (stopped(t)) { result = SDPACKAGES_CANCELLED; break; }
		if (in_at == available && left) {
			available = left < 65536 ? left : 65536;
			if (fread(input, 1, available, t->file) != available) { result = SDPACKAGES_CORRUPT; break; }
			left -= (uint32_t)available; in_at = 0;
		}
		size_t in_bytes = available - in_at, out_bytes = TINFL_LZ_DICT_SIZE - out_at;
		tinfl_status status = tinfl_decompress(inflater, input + in_at, &in_bytes, output,
			output + out_at, &out_bytes, left ? TINFL_FLAG_HAS_MORE_INPUT : 0);
		in_at += in_bytes;
		if (out_bytes > member->bytes - made) { result = SDPACKAGES_CORRUPT; break; }
		size_t written = 0;
		while (written < out_bytes) {
			ssize_t n = write(fd, output + out_at + written, out_bytes - written);
			if (n <= 0) { result = errno == ENOSPC ? SDPACKAGES_NO_SPACE : SDPACKAGES_IO; break; }
			written += (size_t)n;
		}
		if (result != SDPACKAGES_OK) break;
		sha256_update(&sha, output + out_at, out_bytes);
		made += (uint32_t)out_bytes; t->done += out_bytes;
		out_at = (out_at + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
		if (t->progress) t->progress(t->done, t->total, t->user);
		if (status == TINFL_STATUS_DONE) {
			if (left || in_at != available || made != member->bytes) result = SDPACKAGES_CORRUPT;
			break;
		}
		if (status < 0 || (!in_bytes && !out_bytes)) { result = SDPACKAGES_CORRUPT; break; }
	}
	if (result == SDPACKAGES_OK) {
		char actual[65]; sha256_final_hex(&sha, actual);
		if (strcmp(actual, member->sha)) result = SDPACKAGES_CORRUPT;
	}
	if (result == SDPACKAGES_OK && fsync(fd)) result = SDPACKAGES_IO;
	if (close(fd) && result == SDPACKAGES_OK) result = SDPACKAGES_IO;
	if (result == SDPACKAGES_OK && (!strcmp(member->path, "Ebook/Tools/sonix_booktool") || !strcmp(member->path, "Ebook/Tools/mobitool")))
		(void)chmod(path, 0755); // FAT mount masks may provide execute permission instead.
	free(input); free(output); free(inflater);
	return result;
}

sdpackages_result_t sdpackages_install(const char *root, const char *archive, const char *version, const char *expected,
	sdpackages_cancel_t cancel, sdpackages_progress_t progress, void *user) {
	if (!root || !root[0] || !archive || !sdpackages_valid_version(version) || !hex_digest(expected)) return SDPACKAGES_INVALID;
	transfer_t transfer = {.cancel = cancel, .progress = progress, .user = user};
	struct stat st;
	if (lstat(archive, &st) || !S_ISREG(st.st_mode) || st.st_size < 12 || (uint64_t)st.st_size > SDPACKAGES_MAX_BYTES)
		return SDPACKAGES_INVALID;
	FILE *file = fopen(archive, "rb"); if (!file) return SDPACKAGES_IO;
	transfer.file = file;
	unsigned char chunk[65536]; size_t n;
	sha256_ctx sha; sha256_init(&sha);
	while ((n = fread(chunk, 1, sizeof(chunk), file))) {
		if (stopped(&transfer)) { fclose(file); return SDPACKAGES_CANCELLED; }
		sha256_update(&sha, chunk, n);
	}
	char actual[65]; sha256_final_hex(&sha, actual);
	if (ferror(file) || strcmp(actual, expected) || fseek(file, 0, SEEK_SET)) { fclose(file); return SDPACKAGES_CORRUPT; }
	unsigned char header[12];
	if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, "SONIXPK1", 8)) { fclose(file); return SDPACKAGES_INVALID; }
	uint32_t length = (uint32_t)header[8] | (uint32_t)header[9] << 8 | (uint32_t)header[10] << 16 | (uint32_t)header[11] << 24;
	if (!length || length > MANIFEST_MAX || (uint64_t)length + 12 > (uint64_t)st.st_size) { fclose(file); return SDPACKAGES_INVALID; }
	char *manifest = calloc(1, (size_t)length + 1);
	if (!manifest) { fclose(file); return SDPACKAGES_IO; }
	if (fread(manifest, 1, length, file) != length) { free(manifest); fclose(file); return SDPACKAGES_CORRUPT; }
	json_doc_t doc;
	if (memchr(manifest, 0, length) || !json_parse(manifest, &doc)) { free(manifest); fclose(file); return SDPACKAGES_INVALID; }
	int top = json_root(&doc), files = json_get(&doc, top, "files"), count = json_len(&doc, files);
	char declared[SDPACKAGES_VERSION], target[16];
	bool valid = top >= 0 && files >= 0 && doc.toks[top].type == JSON_OBJECT && doc.toks[files].type == JSON_ARRAY &&
		manifest_number(&doc, top, "schema") == 1 && manifest_string(&doc, top, "version", declared, sizeof(declared)) &&
		manifest_string(&doc, top, "target", target, sizeof(target)) && !strcmp(target, "r1") &&
		!strcmp(declared, version) && count > 0 && count <= PACKAGE_FILES;
	member_t *members = valid ? calloc((size_t)count, sizeof(*members)) : NULL;
	uint64_t compressed = 0, expanded = 0;
	for (int i = 0; valid && members && i < count; i++) {
		int item = json_at(&doc, files, i); member_t *m = &members[i];
		long bytes = manifest_number(&doc, item, "bytes"), packed = manifest_number(&doc, item, "compressed");
		valid = item >= 0 && doc.toks[item].type == JSON_OBJECT && bytes >= 0 && (unsigned long)bytes <= 128u * 1024u * 1024u && packed > 0 &&
			(unsigned long)packed <= SDPACKAGES_MAX_BYTES && manifest_string(&doc, item, "path", m->path, sizeof(m->path)) &&
			safe_path(m->path) && manifest_string(&doc, item, "sha256", m->sha, sizeof(m->sha)) && hex_digest(m->sha);
		for (int j = 0; valid && j < i; j++) if (!strcasecmp(m->path, members[j].path)) valid = false;
		if (valid) { m->bytes = (uint32_t)bytes; m->compressed = (uint32_t)packed; expanded += m->bytes; compressed += m->compressed; }
	}
	long declared_bytes = manifest_number(&doc, top, "unpacked_bytes");
	valid = valid && members && expanded <= SDPACKAGES_MAX_BYTES && declared_bytes >= 0 &&
		(uint64_t)declared_bytes == expanded && compressed + length + 12 == (uint64_t)st.st_size;
	json_free(&doc); free(manifest);
	if (!valid) { free(members); fclose(file); return SDPACKAGES_INVALID; }
	struct statvfs vfs;
	if (statvfs(root, &vfs)) { free(members); fclose(file); return SDPACKAGES_IO; }
	uint64_t available = (uint64_t)vfs.f_bavail * (vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
	if (available < expanded + SPACE_MARGIN) { free(members); fclose(file); return SDPACKAGES_NO_SPACE; }
	char sonix[PATH_SIZE], base[PATH_SIZE], stage[PATH_SIZE], final[PATH_SIZE], marker[PATH_SIZE];
	valid = directory(root, false) && join(sonix, sizeof(sonix), root, ".sonix") && directory(sonix, true) &&
		join(base, sizeof(base), sonix, "packages") && directory(base, true);
	int format = valid ? snprintf(stage, sizeof(stage), "%s/stage-XXXXXX", base) : -1;
	if (format < 0 || (size_t)format >= sizeof(stage) || !mkdtemp(stage)) { free(members); fclose(file); return SDPACKAGES_IO; }
	char new_generation[SDPACKAGES_VERSION];
	format = snprintf(new_generation, sizeof(new_generation), "%s-%.8s-%s", version, expected, stage + strlen(stage) - 6);
	if (format < 0 || (size_t)format >= sizeof(new_generation) || !join(final, sizeof(final), base, new_generation)) {
		free(members); fclose(file); rmdir(stage); return SDPACKAGES_INVALID;
	}
	transfer.total = expanded;
	sdpackages_result_t result = SDPACKAGES_OK;
	for (int i = 0; i < count && result == SDPACKAGES_OK; i++) result = extract(&transfer, &members[i], stage);
	free(members); fclose(file);
	if (stopped(&transfer)) result = SDPACKAGES_CANCELLED;
	char old[SDPACKAGES_VERSION] = "", old_version[SDPACKAGES_VERSION], complete[96];
	(void)sdpackages_generation(root, old, old_version);
	if (result == SDPACKAGES_OK) {
		snprintf(complete, sizeof(complete), "SDPKG1 %s\n", version);
		if (!join(marker, sizeof(marker), stage, ".complete") || !write_text(marker, complete) || rename(stage, final)) result = SDPACKAGES_IO;
		else if ((old[0] && !set_pointer(base, "previous", old)) || !set_pointer(base, "active", new_generation)) result = SDPACKAGES_IO;
	}
	unsigned visits = 0;
	(void)remove_tree(stage, 0, &visits);
	if (result == SDPACKAGES_OK) prune(base, new_generation, old);
	return result;
}
