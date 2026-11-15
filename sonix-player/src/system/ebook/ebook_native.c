#define _POSIX_C_SOURCE 200809L
#include "ebook_native.h"
#include "src/system/device/sdpackages.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
static char sd_root[800];
static _Thread_local const char *last_error;
static pthread_mutex_t tools_lock = PTHREAD_MUTEX_INITIALIZER;
static char copied_from[1200], copied_to[1200];

void ebook_set_sd_root(const char *root) {
	if (root && strlen(root) < sizeof(sd_root)) memcpy(sd_root, root, strlen(root) + 1);
}
void ebook_native_error(const char *message) { last_error = message; }
const char *ebook_last_error(void) { return last_error ? last_error : "ebookreader_open_failed"; }

static bool regular(const char *path) {
	struct stat st;
	return !lstat(path, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < 32L * 1024L * 1024L;
}
static bool copy_binary(const char *source, const char *destination) {
	if (!regular(source)) return false;
	FILE *from = fopen(source, "rb"); if (!from) return false;
	int fd = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0700);
	if (fd < 0) { fclose(from); return false; }
	unsigned char bytes[65536]; size_t n; bool ok = true;
	while ((n = fread(bytes, 1, sizeof(bytes), from))) {
		size_t at = 0;
		while (at < n) {
			ssize_t made = write(fd, bytes + at, n - at);
			if (made <= 0) { ok = false; break; }
			at += (size_t)made;
		}
		if (!ok) break;
	}
	if (ferror(from) || fsync(fd)) ok = false;
	fclose(from); if (close(fd)) ok = false;
	if (!ok) unlink(destination);
	return ok;
}
static bool tool_path(char out[1200]) {
#ifdef HOST_BUILD
	const char *override = getenv("SONIX_BOOKTOOL");
	if (override && strlen(override) < 1200 && regular(override)) {
		memcpy(out, override, strlen(override) + 1); return true;
	}
#endif
	char folder[1000], source[1200];
	if (!sdpackages_path(sd_root, "Tools", folder, sizeof(folder))) {
		int n = snprintf(folder, sizeof(folder), "%s/Ebook/Tools", sd_root);
		if (!sd_root[0] || n < 0 || (size_t)n >= sizeof(folder)) return false;
	}
	int n = snprintf(source, sizeof(source), "%s/sonix_booktool", folder);
	if (n < 0 || (size_t)n >= sizeof(source) || !regular(source)) return false;
	if (!access(source, X_OK)) { memcpy(out, source, strlen(source) + 1); return true; }
	// Never stage a large engine on persistent internal storage. /tmp must be
	// RAM-backed when the card's mount policy requires an executable copy.
#ifndef HOST_BUILD
	struct statfs temporary;
	if (statfs("/tmp", &temporary) || (unsigned long)temporary.f_type != 0x01021994UL) return false;
#endif
	// A card mounted noexec still stores the engines. Copy this verified
	// managed generation to RAM-backed /tmp; never change its mount flags.
	pthread_mutex_lock(&tools_lock);
	bool ok = !strcmp(copied_from, folder) && copied_to[0] && !access(copied_to, X_OK);
	if (!ok) {
		char temp[] = "/tmp/sonix-readers-XXXXXX", target[1200], mobi_source[1200], mobi_target[1200];
		if (mkdtemp(temp)) {
			n = snprintf(target, sizeof(target), "%s/sonix_booktool", temp);
			ok = n > 0 && (size_t)n < sizeof(target) && copy_binary(source, target);
			if (ok) {
				snprintf(mobi_source, sizeof(mobi_source), "%s/mobitool", folder);
				snprintf(mobi_target, sizeof(mobi_target), "%s/mobitool", temp);
				if (regular(mobi_source)) ok = copy_binary(mobi_source, mobi_target);
			}
			if (ok) { memcpy(copied_from, folder, strlen(folder) + 1); memcpy(copied_to, target, strlen(target) + 1); }
		}
	}
	if (ok) memcpy(out, copied_to, strlen(copied_to) + 1);
	pthread_mutex_unlock(&tools_lock);
	return ok;
}
static int64_t now_ms(void) {
	struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static bool read_exact(int fd, void *target, size_t size, int64_t deadline) {
	unsigned char *bytes = target;
	while (size) {
		int64_t left = deadline - now_ms();
		if (left <= 0) return false;
		struct pollfd p = {.fd = fd, .events = POLLIN};
		int ready = poll(&p, 1, left > 1000 ? 1000 : (int)left);
		if (ready < 0 && errno == EINTR) continue;
		if (ready < 0) return false;
		if (!ready) continue;
		ssize_t n = read(fd, bytes, size);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return false;
		bytes += n; size -= (size_t)n;
	}
	return true;
}
bool ebook_native_call(const char *path, const char *operation, uint32_t page, epub_arena_t *arena, ebook_native_result_t *out) {
	memset(out, 0, sizeof(*out));
	char tool[1200], number[32];
	if (!tool_path(tool)) { last_error = "Install the native reader package through Settings > System > Update firmware."; return false; }
	snprintf(number, sizeof(number), "%u", page);
	char *args[] = {tool, (char *)operation, number, (char *)path, sd_root, NULL};
	int pipe_fd[2];
	if (pipe(pipe_fd)) return false;
	posix_spawn_file_actions_t actions; posix_spawnattr_t attr;
	posix_spawn_file_actions_init(&actions); posix_spawnattr_init(&attr);
	posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
	posix_spawn_file_actions_addclose(&actions, pipe_fd[1]);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
	posix_spawnattr_setpgroup(&attr, 0);
	pid_t child;
	int spawned = posix_spawn(&child, tool, &actions, &attr, args, environ);
	posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr);
	close(pipe_fd[1]);
	if (spawned) { close(pipe_fd[0]); last_error = "The native reader package could not start. Reinstall it from Updates."; return false; }
	int64_t deadline = now_ms() + 45000;
	unsigned char header[BOOKTOOL_HEADER];
	bool ok = read_exact(pipe_fd[0], header, sizeof(header), deadline) && !memcmp(header, BOOKTOOL_MAGIC, 4);
	unsigned status = ok ? booktool_u32(header + 4) : BOOKTOOL_BAD_FILE;
	if (ok && status != BOOKTOOL_OK) {
		last_error = status == BOOKTOOL_PROTECTED ? "This book is password-protected or DRM-protected." :
			status == BOOKTOOL_LIMIT ? "This book exceeds the R1 reader's resource limits." : "This book could not be read.";
		ok = false;
	}
	if (ok) {
		out->pages = booktool_u32(header + 8); out->flags = booktool_u32(header + 12);
		out->text_size = booktool_u32(header + 16); out->png_size = booktool_u32(header + 20);
		ok = out->pages <= BOOKTOOL_PAGES_MAX && out->text_size <= BOOKTOOL_TEXT_MAX && out->png_size <= BOOKTOOL_IMAGE_MAX &&
			header[279] == 0 && header[407] == 0 && !(out->flags & ~(BOOKTOOL_FIXED | BOOKTOOL_EPUB));
	}
	if (ok) {
		memcpy(out->title, header + 24, sizeof(out->title)); memcpy(out->author, header + 280, sizeof(out->author));
		if (out->text_size) {
			out->text = arena_alloc(arena, (size_t)out->text_size + 1);
			ok = out->text && read_exact(pipe_fd[0], out->text, out->text_size, deadline);
			if (ok) { out->text[out->text_size] = 0; if (memchr(out->text, 0, out->text_size)) ok = false; }
		}
		if (ok && out->png_size) {
			out->png = arena_alloc(arena, out->png_size);
			ok = out->png && read_exact(pipe_fd[0], out->png, out->png_size, deadline);
			if (ok) {
				ok = out->png_size >= 33 && !memcmp(out->png, "\x89PNG\r\n\x1a\n", 8) && !memcmp(out->png + 12, "IHDR", 4);
				if (ok) {
					const unsigned char *p = out->png + 16;
					uint32_t width = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
					uint32_t height = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
					ok = width && width <= 962 && height && height <= 1282;
				}
			}
		}
	}
	close(pipe_fd[0]);
	int result = 0;
	bool reaped = false;
	while (ok && now_ms() < deadline) {
		pid_t waited = waitpid(child, &result, WNOHANG);
		if (waited == child) { reaped = true; break; }
		if (waited < 0 && errno != EINTR) { ok = false; break; }
		(void)poll(NULL, 0, 10);
	}
	if (!reaped) {
		ok = false;
		kill(-child, SIGKILL);
		while (waitpid(child, &result, 0) < 0 && errno == EINTR) { }
	}
	if (!WIFEXITED(result) || WEXITSTATUS(result)) ok = false;
	if (!ok && !last_error) last_error = "This book could not be opened within the R1 reader limits.";
	return ok;
}
