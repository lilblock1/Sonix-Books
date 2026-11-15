#include "ota.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "src/system/core/json.h"
#include "src/system/core/config.h"
#include "src/system/core/sha256.h"
#include "src/system/core/utils.h"
#include "src/system/device/firmware.h"
#include "src/system/device/ota_policy.h"
#include "src/system/device/sdpackages.h"
#include "src/system/device/sysinfo.h"
#include "src/system/device/system.h"
#include "src/system/net/http.h"
#include "src/system/net/wifi.h"

#define DEFAULT_REPO "lilblock1/Sonix-Books"

// The API's own media type and version, as GitHub asks every client to send.
#define API_HEADERS                                                                                                    \
	"Accept: application/vnd.github+json\r\n"                                                                          \
	"X-GitHub-Api-Version: 2022-11-28\r\n"

// Twenty releases with their descriptions stay well under this.
#define LIST_LIMIT (1536 * 1024)
#define LIST_TIMEOUT_SECS 20
#define DOWNLOAD_TIMEOUT_SECS 30

// Room left on the card beyond the file itself.
#define SPACE_MARGIN (8L * 1024 * 1024)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ota_state_t state = OTA_IDLE;
static ota_result_t result = OTA_OK;
static long progress_done, progress_total;
static bool cancel_requested;
static http_stream_t *active_stream; // the download's stream, for ota_cancel() to wake

static ota_release_t release = {.notes = ""};
static bool release_valid;
// What release.notes points at when the release has a description: as long
// as the description, so a long changelog is not cut at a fixed size.
static char *notes_text;

// Set on the interface thread when a check starts.
static char repo[128];
static char stem[64];
static bool checking_test;
static unsigned extras_mask;
static bool store_mode, store_valid;
static game_store_item_t store_items[GAME_STORE_MAX];
static size_t store_count;
static unsigned store_index;
static struct { long size; char url[512], sha[65]; } extras[SD_EXTRAS_COUNT];

static unsigned char chunk[64 * 1024];

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

static bool begin(ota_state_t next) {
	pthread_mutex_lock(&lock);
	bool ok = state == OTA_IDLE || state == OTA_FINISHED;
	if (ok) {
		state = next;
		cancel_requested = false;
		progress_done = 0;
		progress_total = 0;
	}
	pthread_mutex_unlock(&lock);
	return ok;
}

static void finish(ota_result_t r) {
	pthread_mutex_lock(&lock);
	result = cancel_requested && r != OTA_OK ? OTA_CANCELLED : r;
	state = OTA_FINISHED;
	active_stream = NULL;
	pthread_mutex_unlock(&lock);
}

static bool cancelled(void) {
	pthread_mutex_lock(&lock);
	bool c = cancel_requested;
	pthread_mutex_unlock(&lock);
	return c;
}

void ota_cancel(void) {
	pthread_mutex_lock(&lock);
	if (state == OTA_CHECKING || state == OTA_DOWNLOADING) {
		cancel_requested = true;
		if (active_stream) {
			http_stream_wake(active_stream);
		}
	}
	pthread_mutex_unlock(&lock);
}

ota_state_t ota_state(void) {
	pthread_mutex_lock(&lock);
	ota_state_t s = state;
	pthread_mutex_unlock(&lock);
	return s;
}

ota_result_t ota_result(void) {
	pthread_mutex_lock(&lock);
	ota_result_t r = result;
	pthread_mutex_unlock(&lock);
	return r;
}

void ota_acknowledge(void) {
	pthread_mutex_lock(&lock);
	if (state == OTA_FINISHED) {
		state = OTA_IDLE;
	}
	pthread_mutex_unlock(&lock);
}

bool ota_busy(void) {
	ota_state_t s = ota_state();
	return s == OTA_CHECKING || s == OTA_DOWNLOADING;
}

void ota_progress(long *done, long *total) {
	pthread_mutex_lock(&lock);
	if (done) {
		*done = progress_done;
	}
	if (total) {
		*total = progress_total;
	}
	pthread_mutex_unlock(&lock);
}

const ota_release_t *ota_release(void) { return &release; }

const char *ota_installed_version(void) { return sysinfo_os_version(); }
bool ota_test_channel(void) { return !strcmp(config_get("updates", "channel", "stable"), "test"); }
bool ota_set_test_channel(bool enabled) {
	if (ota_busy()) return false;
	config_set("updates", "channel", enabled ? "test" : "stable");
	config_save();
	release_valid = false;
	return true;
}

// ---------------------------------------------------------------------------
// versions
// ---------------------------------------------------------------------------

#define VERSION_PARTS 4

// ---------------------------------------------------------------------------
// the release list
// ---------------------------------------------------------------------------

// Drops a UTF-8 sequence cut in half at the end of `s`.
static void trim_partial_utf8(char *s) {
	size_t len = strlen(s);
	size_t i = len;
	while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
		i--;
	}
	if (i == 0) {
		return;
	}
	unsigned char lead = (unsigned char)s[i - 1];
	size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
	if (len - (i - 1) < need) {
		s[i - 1] = '\0';
	}
}

// The asset named "<stem>.upt" in release `rel`, -1 when there is none.
static int find_named_asset(const json_doc_t *doc, int rel, const char *wanted) {
	int assets = json_get(doc, rel, "assets");
	int count = json_len(doc, assets);
	for (int i = 0; i < count; i++) {
		int a = json_at(doc, assets, i);
		char name[96], asset_state[24];
		json_obj_str(doc, a, "name", name, sizeof(name));
		json_obj_str(doc, a, "state", asset_state, sizeof(asset_state));
		if (strcasecmp(name, wanted) == 0 && strcmp(asset_state, "uploaded") == 0) {
			return a;
		}
	}
	return -1;
}

static int find_asset(const json_doc_t *doc, int rel) {
	char wanted[80]; snprintf(wanted, sizeof(wanted), "%s.upt", stem);
	return find_named_asset(doc, rel, wanted);
}

static ota_result_t pick_release(const json_doc_t *doc) {
	int root = json_root(doc);
	if (root < 0 || doc->toks[root].type != JSON_ARRAY) {
		return OTA_CHECK_FAILED;
	}

	long installed[VERSION_PARTS];
	if (!ota_policy_version(ota_installed_version(), installed)) {
		memset(installed, 0, sizeof(installed));
	}

	long best[VERSION_PARTS];
	memcpy(best, installed, sizeof(best));
	int best_rel = -1, best_asset = -1;
	int same_rel = -1, same_asset = -1;

	int count = json_len(doc, root);
	for (int i = 0; i < count; i++) {
		int rel = json_at(doc, root, i);
		char tag[32];
		long version[VERSION_PARTS];
		if (!json_obj_str(doc, rel, "tag_name", tag, sizeof(tag)) || !ota_policy_version(tag, version) ||
			!ota_policy_release(checking_test, json_obj_bool(doc, rel, "draft", true),
				json_obj_bool(doc, rel, "prerelease", true), tag)) {
			continue;
		}
		// Selectable release assets belong to the Extras menu; do not force
		// their combined bundle through a firmware-only check.
		bool split = true;
		for (unsigned k = 0; k < SD_EXTRAS_COUNT; ++k)
			if (find_named_asset(doc, rel, sdpackages_extra_asset(k)) < 0) split = false;
		if (!split && ota_policy_version_cmp(version, installed) == 0 && find_named_asset(doc, rel, SDPACKAGES_ASSET) >= 0) {
			char active[SDPACKAGES_VERSION];
			if (!sdpackages_active(storage_sd_root(), active) || strcmp(active, tag)) { same_rel = rel; same_asset = find_asset(doc, rel); }
		}
		if (ota_policy_version_cmp(version, best) <= 0) {
			continue;
		}
		int asset = find_asset(doc, rel);
		if (asset < 0) {
			printf("ota: %s has no %s.upt, skipped\n", tag, stem);
			continue;
		}
		memcpy(best, version, sizeof(best));
		best_rel = rel;
		best_asset = asset;
	}

	if (best_rel < 0) {
		if (same_rel < 0 || same_asset < 0) return OTA_UP_TO_DATE;
		best_rel = same_rel; best_asset = same_asset;
	}

	memset(&release, 0, sizeof(release));
	release.notes = "";
	release.packages_only = ota_policy_version_cmp(best, installed) == 0;
	release.test_build = json_obj_bool(doc, best_rel, "prerelease", true);
	free(notes_text);
	notes_text = NULL;
	json_obj_str(doc, best_rel, "tag_name", release.tag, sizeof(release.tag));
	if (!json_obj_str(doc, best_rel, "name", release.name, sizeof(release.name)) || !release.name[0]) {
		snprintf(release.name, sizeof(release.name), "%s", release.tag);
	}
	// Unescaped, the text is never longer than it is in the JSON.
	int body = json_get(doc, best_rel, "body");
	if (body >= 0 && doc->toks[body].type == JSON_STRING) {
		size_t size = (size_t)(doc->toks[body].end - doc->toks[body].start) + 1;
		if (size > OTA_NOTES_MAX) {
			size = OTA_NOTES_MAX;
		}
		notes_text = malloc(size);
		if (notes_text && json_str(doc, body, notes_text, size)) {
			trim_partial_utf8(notes_text);
			release.notes = notes_text;
		}
	}
	release.size = json_obj_long(doc, best_asset, "size", 0);
	json_obj_str(doc, best_asset, "browser_download_url", release.url, sizeof(release.url));

	// "sha256:<64 hex>" on assets uploaded since mid-2025.
	char digest[80];
	if (!json_obj_str(doc, best_asset, "digest", digest, sizeof(digest)) || !ota_policy_digest(digest)) {
		printf("ota: release has no valid GitHub SHA-256 digest; refusing it\n");
		return OTA_CHECK_FAILED;
	}
	snprintf(release.sha256, sizeof(release.sha256), "%s", digest + 7);

	if (!ota_policy_asset(repo, stem, release.url, release.size)) {
		return OTA_CHECK_FAILED;
	}
	int package = find_named_asset(doc, best_rel, SDPACKAGES_ASSET);
	bool split = true;
	for (unsigned k = 0; k < SD_EXTRAS_COUNT; ++k)
		if (find_named_asset(doc, best_rel, sdpackages_extra_asset(k)) < 0) split = false;
	if (package >= 0 && !split) {
		release.packages_size = json_obj_long(doc, package, "size", 0);
		json_obj_str(doc, package, "browser_download_url", release.packages_url, sizeof(release.packages_url));
		if (!json_obj_str(doc, package, "digest", digest, sizeof(digest)) || !ota_policy_digest(digest) ||
			!ota_policy_package(repo, release.tag, release.packages_url, release.packages_size)) return OTA_CHECK_FAILED;
		memcpy(release.packages_sha256, digest + 7, 65);
	}
	release_valid = true;
	printf("ota: %s (%s) available, %ld bytes, sha256 %s\n", release.tag, release.name, release.size,
		   release.sha256[0] ? release.sha256 : "not published");
	return OTA_OK;
}

unsigned ota_extras_missing(unsigned mask) {
	if (!storage_card_attached()) return mask & SD_EXTRAS_MASK;
	long installed[4], existing[4];
	if (!ota_policy_version(ota_installed_version(), installed)) return mask & SD_EXTRAS_MASK;
	unsigned missing = 0;
	for (unsigned k = 0; k < SD_EXTRAS_COUNT; ++k) {
		if (!(mask & (1u << k))) continue;
		char version[SDPACKAGES_VERSION];
		if (!sdpackages_component_version(storage_sd_root(), k, version) ||
			!ota_policy_version(version, existing) || ota_policy_version_cmp(installed, existing))
			missing |= 1u << k;
	}
	return missing;
}

static ota_result_t pick_extras(const json_doc_t *doc) {
	int root = json_root(doc);
	long installed[4];
	if (root < 0 || doc->toks[root].type != JSON_ARRAY ||
		!ota_policy_version(ota_installed_version(), installed)) return OTA_CHECK_FAILED;
	// Prefer stable when the same numeric firmware has been promoted.
	// A test-channel installation may still use its exact matching test release.
	for (int pass = 0; pass < 2; ++pass) {
	if (pass && !checking_test) break;
	for (int i = 0; i < json_len(doc, root); ++i) {
		int rel = json_at(doc, root, i);
		if (json_obj_bool(doc, rel, "prerelease", true) != (pass != 0)) continue;
		char tag[32];
		long version[4];
		if (!json_obj_str(doc, rel, "tag_name", tag, sizeof(tag)) ||
			!ota_policy_version(tag, version) || ota_policy_version_cmp(installed, version) ||
			!ota_policy_release(true, json_obj_bool(doc, rel, "draft", true),
				json_obj_bool(doc, rel, "prerelease", true), tag) || find_asset(doc, rel) < 0) continue;
		bool complete = true;
		for (unsigned k = 0; k < SD_EXTRAS_COUNT; ++k)
			if ((extras_mask & (1u << k)) && find_named_asset(doc, rel, sdpackages_extra_asset(k)) < 0) complete = false;
		if (!complete) continue;
		memset(extras, 0, sizeof(extras));
		long total = 0;
		for (unsigned k = 0; k < SD_EXTRAS_COUNT; ++k) {
			if (!(extras_mask & (1u << k))) continue;
			const char *name = sdpackages_extra_asset(k);
			int asset = find_named_asset(doc, rel, name);
			if (asset < 0) return OTA_EXTRAS_UNAVAILABLE;
			char digest[80], expected[512];
			extras[k].size = json_obj_long(doc, asset, "size", 0);
			int n = snprintf(expected, sizeof(expected), "https://github.com/%s/releases/download/%s/%s", repo, tag, name);
			if (n < 0 || (size_t)n >= sizeof(expected) || extras[k].size <= 0 ||
				(unsigned long)extras[k].size > SDPACKAGES_MAX_BYTES ||
				!json_obj_str(doc, asset, "browser_download_url", extras[k].url, sizeof(extras[k].url)) ||
				strcmp(expected, extras[k].url) ||
				!json_obj_str(doc, asset, "digest", digest, sizeof(digest)) || !ota_policy_digest(digest)) return OTA_CORRUPT;
			memcpy(extras[k].sha, digest + 7, 65);
			total += extras[k].size;
		}
		memset(&release, 0, sizeof(release));
		release.notes = "";
		release.packages_only = true;
		memcpy(release.tag, tag, strlen(tag) + 1);
		pthread_mutex_lock(&lock);
		progress_total = total;
		pthread_mutex_unlock(&lock);
		return OTA_OK;
	}
	}
	return OTA_EXTRAS_UNAVAILABLE;
}

static ota_result_t do_check(void) {
	// The reader may have just restored a previously enabled radio. Wait
	// on this worker, never the UI thread, for association and DHCP. Do
	// not override a radio the user explicitly switched off.
	bool online = false;
	for (unsigned attempt = 0; attempt <= 40; ++attempt) {
		if (cancelled()) return OTA_CANCELLED;
		bool enabled = wifi_get_enabled();
		if (!enabled && !config_get_int("wireless", "wifi", 0)) break;
		char ip[40];
		if (enabled && wifi_interface_address(ip, sizeof(ip))) {
			online = true;
			break;
		}
		if (attempt < 40) usleep(500 * 1000);
	}
	if (!online) return OTA_NO_NETWORK;
	if (!stem[0]) {
		return OTA_NO_MODEL;
	}

	char url[256];
	snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases?per_page=20", repo);
	printf("ota: checking %s, installed '%s', file %s.upt\n", url, ota_installed_version(), stem);

	char *body = NULL;
	size_t body_len = 0;
	int status = 0;
	if (!http_get_ex(url, API_HEADERS, false, &body, &body_len, LIST_LIMIT, LIST_TIMEOUT_SECS, &status)) {
		const char *why = http_last_error();
		printf("ota: release list not fetched (status %d%s%s)\n", status, why ? ", " : "", why ? why : "");
		return OTA_CHECK_FAILED;
	}
	if (cancelled()) {
		free(body);
		return OTA_CANCELLED;
	}

	json_doc_t doc;
	ota_result_t r = OTA_CHECK_FAILED;
	if (json_parse(body, &doc)) {
		r = extras_mask ? pick_extras(&doc) : pick_release(&doc);
		json_free(&doc);
	} else {
		printf("ota: release list is not valid JSON (%zu bytes)\n", body_len);
	}
	free(body);
	return r;
}

static void *check_thread(void *unused) {
	(void)unused;
	thread_be_background("ota check");
	finish(do_check());
	return NULL;
}

bool ota_check_start(void) {
	if (!begin(OTA_CHECKING)) {
		return false;
	}
	release_valid = false;
	store_mode = store_valid = false;
	extras_mask = 0;
	checking_test = ota_test_channel();

	const char *r = sysinfo_ota_repo();
	snprintf(repo, sizeof(repo), "%s", r[0] ? r : DEFAULT_REPO);
	const char *s = firmware_update_stem();
	snprintf(stem, sizeof(stem), "%s", s ? s : "");

	pthread_t thread;
	if (pthread_create(&thread, NULL, check_thread, NULL) != 0) {
		finish(OTA_CHECK_FAILED);
		return true;
	}
	pthread_detach(thread);
	return true;
}

// ---------------------------------------------------------------------------
// the download
// ---------------------------------------------------------------------------

// Removes every "<stem>.upt" on the card, whatever its case: on exFAT two
// spellings can sit side by side, and the recovery kernel would take either.
static void remove_old_files(const char *root) {
	char wanted[80];
	snprintf(wanted, sizeof(wanted), "%s.upt", stem);

	DIR *dir = opendir(root);
	if (!dir) {
		return;
	}
	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {
		if (strcasecmp(de->d_name, wanted) == 0) {
			char path[600];
			snprintf(path, sizeof(path), "%s/%s", root, de->d_name);
			unlink(path);
		}
	}
	closedir(dir);
}

static ota_result_t download_asset(const char *url, long size, const char *expected, const char *part, long base) {
	int fd = open(part, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (fd < 0) return OTA_DOWNLOAD_FAILED;
	FILE *f = fdopen(fd, "wb");
	if (!f) { close(fd); unlink(part); return OTA_DOWNLOAD_FAILED; }
	http_stream_t stream; memset(&stream, 0, sizeof(stream)); stream.fd = -1;
	if (!http_stream_open(&stream, url, DOWNLOAD_TIMEOUT_SECS)) {
		fclose(f); unlink(part); return cancelled() ? OTA_CANCELLED : OTA_DOWNLOAD_FAILED;
	}
	pthread_mutex_lock(&lock); active_stream = &stream; pthread_mutex_unlock(&lock);
	ota_result_t r = OTA_OK;
	if (stream.content_length > 0 && stream.content_length != size) r = OTA_CORRUPT;
	sha256_ctx sha; sha256_init(&sha);
	long done = 0;
	while (r == OTA_OK && done < size && !cancelled()) {
		long want = size - done;
		int n = http_stream_read(&stream, chunk, want < (long)sizeof(chunk) ? (int)want : (int)sizeof(chunk));
		if (n <= 0 || fwrite(chunk, 1, (size_t)n, f) != (size_t)n) { r = OTA_DOWNLOAD_FAILED; break; }
		sha256_update(&sha, chunk, (size_t)n); done += n;
		pthread_mutex_lock(&lock); progress_done = base + done; pthread_mutex_unlock(&lock);
	}
	pthread_mutex_lock(&lock); active_stream = NULL; pthread_mutex_unlock(&lock);
	http_stream_close(&stream);
	if (fflush(f) || fsync(fileno(f))) r = OTA_DOWNLOAD_FAILED;
	if (fclose(f)) r = OTA_DOWNLOAD_FAILED;
	if (cancelled()) r = OTA_CANCELLED;
	if (r == OTA_OK) {
		char digest[65]; sha256_final_hex(&sha, digest);
		if (done != size || strcasecmp(digest, expected)) r = OTA_CORRUPT;
	}
	if (r != OTA_OK) unlink(part);
	return r;
}
static bool package_cancel(void *user) { (void)user; return cancelled(); }
static void package_progress(uint64_t done, uint64_t total, void *user) {
	(void)user;
	long downloads = (release.packages_only ? 0 : release.size) + release.packages_size;
	pthread_mutex_lock(&lock);
	progress_done = downloads + (long)done; progress_total = downloads + (long)total;
	pthread_mutex_unlock(&lock);
}
static ota_result_t download_to_card(const char *root, const struct statvfs *vfs);

static ota_result_t download_extras_to_card(const char *root) {
	long done = 0;
	for (unsigned k = 0; k < SD_EXTRAS_COUNT; ++k) {
		if (!(extras_mask & (1u << k))) continue;
		if (cancelled()) return OTA_CANCELLED;
		struct statvfs vfs;
		if (statvfs(root, &vfs)) return OTA_NO_CARD;
		uint64_t space = (uint64_t)vfs.f_bavail * (vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
		if (space < (uint64_t)extras[k].size + SPACE_MARGIN) return OTA_NO_SPACE;
		// Unique scratch path on the pinned SD filesystem, never a firmware filename.
		char part[120];
		int n = snprintf(part, sizeof(part), "%s/.sonix-extra-XXXXXX", root);
		if (n <= 0 || (size_t)n >= sizeof(part)) return OTA_DOWNLOAD_FAILED;
		int fd = mkstemp(part);
		if (fd < 0) return OTA_DOWNLOAD_FAILED;
		close(fd);
		ota_result_t r = download_asset(extras[k].url, extras[k].size, extras[k].sha, part, done);
		if (r == OTA_OK) {
			sdpackages_result_t s = sdpackages_install_extra(root, k, part, release.tag, extras[k].sha,
				package_cancel, NULL, NULL);
			r = s == SDPACKAGES_OK ? OTA_OK : s == SDPACKAGES_CANCELLED ? OTA_CANCELLED :
				s == SDPACKAGES_NO_SPACE ? OTA_NO_SPACE :
				s == SDPACKAGES_INVALID || s == SDPACKAGES_CORRUPT ? OTA_CORRUPT : OTA_DOWNLOAD_FAILED;
		}
		unlink(part);
		if (r != OTA_OK) return r;
		done += extras[k].size;
	}
	return OTA_OK;
}

static ota_result_t download_game_to_card(const char *root, const struct statvfs *vfs) {
	const game_store_item_t *item = &store_items[store_index];
	uint64_t space = (uint64_t)vfs->f_bavail * (vfs->f_frsize ? vfs->f_frsize : vfs->f_bsize);
	if (space < (uint64_t)item->size + SPACE_MARGIN) return OTA_NO_SPACE;
	char generation[SDPACKAGES_VERSION], version[SDPACKAGES_VERSION];
	if (sdpackages_game_generation(root, item->id, generation, version) &&
		game_store_version_cmp(item->version, version) < 0) return OTA_UP_TO_DATE;
	char part[120];
	int n=snprintf(part,sizeof(part),"%s/.sonix-game-XXXXXX",root);
	if(n<=0 || (size_t)n>=sizeof(part)) return OTA_DOWNLOAD_FAILED;
	int fd=mkstemp(part);
	if (fd<0) return OTA_DOWNLOAD_FAILED;
	close(fd);
	ota_result_t r=download_asset(item->url,item->size,item->sha256,part,0);
	if (r==OTA_OK) {
		sdpackages_result_t s=sdpackages_install_game(root,item->id,part,item->version,item->sha256,package_cancel,NULL,NULL);
		r=s==SDPACKAGES_OK?OTA_OK:s==SDPACKAGES_CANCELLED?OTA_CANCELLED:
			s==SDPACKAGES_NO_SPACE?OTA_NO_SPACE:s==SDPACKAGES_CORRUPT||s==SDPACKAGES_INVALID?OTA_CORRUPT:OTA_DOWNLOAD_FAILED;
	}
	unlink(part);
	return r;
}

static ota_result_t do_download(void) {
	if (!extras_mask && !store_mode) {
	char digest[80];
	snprintf(digest, sizeof(digest), "sha256:%s", release.sha256);
	if (!ota_policy_digest(digest) || !ota_policy_asset(repo, stem, release.url, release.size)) return OTA_CHECK_FAILED;
	if (release.packages_size) {
		snprintf(digest, sizeof(digest), "sha256:%s", release.packages_sha256);
		if (!ota_policy_digest(digest) || !ota_policy_package(repo, release.tag, release.packages_url, release.packages_size)) return OTA_CHECK_FAILED;
	}
	}
	const char *mount_path = storage_sd_root();
	if (!mount_path || !mount_path[0] || !storage_card_attached()) return OTA_NO_CARD;

	// An unmounted SD mount point is an ordinary directory on internal flash.
	// Open the card first and require a separate filesystem at its mount root.
	// Pin that directory for the whole transaction: if the card is unmounted
	// or replaced, our path still refers to the original filesystem, never
	// the exposed internal directory or a newly inserted card.
	int card = open(mount_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (card < 0) return OTA_NO_CARD;
	struct stat mounted, parent;
	struct statvfs vfs;
	if (fstat(card, &mounted) || fstatat(card, "..", &parent, 0) ||
		mounted.st_dev == parent.st_dev || fstatvfs(card, &vfs) ||
		(vfs.f_flag & ST_RDONLY)) {
		close(card);
		return OTA_NO_CARD;
	}
	// The trailing /. yields the actual directory rather than the procfs
	// descriptor symlink (the SD package installer rejects symlink roots).
	char root[64];
	int root_length = snprintf(root, sizeof(root), "/proc/self/fd/%d/.", card);
	if (root_length < 0 || (size_t)root_length >= sizeof(root)) {
		close(card);
		return OTA_NO_CARD;
	}
	ota_result_t result = store_mode ? download_game_to_card(root, &vfs) : extras_mask ? download_extras_to_card(root) : download_to_card(root, &vfs);
	close(card);
	return result;
}

static ota_result_t download_to_card(const char *root, const struct statvfs *vfs) {
	uint64_t free_bytes = (uint64_t)vfs->f_bavail * (vfs->f_frsize ? vfs->f_frsize : vfs->f_bsize);
	long firmware_bytes = release.packages_only ? 0 : release.size;
	// Installer checks expanded-space requirements separately, before staging.
	if (free_bytes < (uint64_t)firmware_bytes + (uint64_t)release.packages_size + SPACE_MARGIN) return OTA_NO_SPACE;
	char final[600], part[620], package[1000];
	int n = snprintf(final, sizeof(final), "%s/%s.upt", root, stem);
	if (n < 0 || (size_t)n >= sizeof(final)) return OTA_DOWNLOAD_FAILED;
	n = snprintf(part, sizeof(part), "%s.part", final);
	if (n < 0 || (size_t)n >= sizeof(part)) return OTA_DOWNLOAD_FAILED;
	n = snprintf(package, sizeof(package), "%s/.sonix-SD-Packages.part", root);
	if (n < 0 || (size_t)n >= sizeof(package)) return OTA_DOWNLOAD_FAILED;
	ota_result_t r = OTA_OK;
	if (firmware_bytes) r = download_asset(release.url, release.size, release.sha256, part, 0);
	if (r == OTA_OK && release.packages_size) {
		r = download_asset(release.packages_url, release.packages_size, release.packages_sha256, package, firmware_bytes);
		if (r == OTA_OK) {
			sdpackages_result_t installed = sdpackages_install(root, package, release.tag, release.packages_sha256,
				package_cancel, package_progress, NULL);
			r = installed == SDPACKAGES_OK ? OTA_OK : installed == SDPACKAGES_CANCELLED ? OTA_CANCELLED :
				installed == SDPACKAGES_NO_SPACE ? OTA_NO_SPACE :
				installed == SDPACKAGES_INVALID || installed == SDPACKAGES_CORRUPT ? OTA_CORRUPT : OTA_DOWNLOAD_FAILED;
		}
		unlink(package);
	}
	if (r == OTA_OK && cancelled()) r = OTA_CANCELLED;
	if (firmware_bytes) {
		if (r == OTA_OK) {
			remove_old_files(root);
			if (rename(part, final)) r = OTA_DOWNLOAD_FAILED;
		}
		if (r != OTA_OK) unlink(part);
	}
	if (r == OTA_OK) sync();
	return r;
}

static void *download_thread(void *unused) {
	(void)unused;
	thread_be_background("ota download");
	finish(do_download());
	return NULL;
}

bool ota_download_start(void) {
	if (extras_mask || store_mode) return false;
	if (!release_valid || !begin(OTA_DOWNLOADING)) {
		return false;
	}

	pthread_mutex_lock(&lock);
	progress_total = (release.packages_only ? 0 : release.size) + release.packages_size;
	pthread_mutex_unlock(&lock);

	pthread_t thread;
	if (pthread_create(&thread, NULL, download_thread, NULL) != 0) {
		finish(OTA_DOWNLOAD_FAILED);
		return true;
	}
	pthread_detach(thread);
	return true;
}

static void *extras_thread(void *unused) {
	(void)unused;
	thread_be_background("extras download");
	ota_result_t r = do_check();
	if (r == OTA_OK && !cancelled()) {
		pthread_mutex_lock(&lock);
		state = OTA_DOWNLOADING;
		pthread_mutex_unlock(&lock);
		r = do_download();
	} else if (cancelled()) r = OTA_CANCELLED;
	finish(r);
	return NULL;
}

bool ota_extras_start(unsigned mask) {
	if (!mask || (mask & ~SD_EXTRAS_MASK) || !begin(OTA_CHECKING)) return false;
	store_mode = store_valid = false;
	extras_mask = mask;
	checking_test = ota_test_channel();
	release_valid = false;
	const char *r = sysinfo_ota_repo();
	snprintf(repo, sizeof(repo), "%s", r && r[0] ? r : DEFAULT_REPO);
	const char *s = firmware_update_stem();
	snprintf(stem, sizeof(stem), "%s", s ? s : "");
	pthread_t thread;
	if (pthread_create(&thread, NULL, extras_thread, NULL)) finish(OTA_CHECK_FAILED);
	else pthread_detach(thread);
	return true;
}

static void *store_check_thread(void *unused) {
	(void)unused;
	thread_be_background("game store");
	char ip[40];
	if (!wifi_get_enabled() || !wifi_interface_address(ip,sizeof(ip))) {
		finish(OTA_NO_NETWORK); return NULL;
	}
	char *body=NULL;
	size_t length=0; int status=0;
	const char *url="https://api.github.com/repos/" GAME_STORE_REPO "/releases/tags/" GAME_STORE_TAG;
	bool ok=http_get_ex(url,API_HEADERS,false,&body,&length,GAME_STORE_JSON_MAX,LIST_TIMEOUT_SECS,&status);
	if (ok && !cancelled() && length<GAME_STORE_JSON_MAX && !strncmp(http_last_final_url(),"https://api.github.com/",23) &&
		game_store_parse(body,store_items,GAME_STORE_MAX,&store_count)) store_valid=true;
	free(body);
	finish(cancelled()?OTA_CANCELLED:store_valid?OTA_OK:OTA_CHECK_FAILED);
	return NULL;
}

size_t ota_store_count(void) { return store_valid ? store_count : 0; }
const game_store_item_t *ota_store_item(unsigned index) {
	return store_valid && index<store_count ? &store_items[index] : NULL;
}

bool ota_store_check_start(void) {
	if (!begin(OTA_CHECKING)) return false;
	store_mode=true; store_valid=release_valid=false; store_count=0; extras_mask=0;
	pthread_t thread;
	if(pthread_create(&thread,NULL,store_check_thread,NULL)) finish(OTA_CHECK_FAILED);
	else pthread_detach(thread);
	return true;
}

bool ota_store_download_start(unsigned index) {
	if(!store_mode || !store_valid || index>=store_count || !begin(OTA_DOWNLOADING)) return false;
	store_index=index;
	pthread_mutex_lock(&lock);
	progress_total=store_items[index].size;
	pthread_mutex_unlock(&lock);
	pthread_t thread;
	if(pthread_create(&thread,NULL,download_thread,NULL)) finish(OTA_DOWNLOAD_FAILED);
	else pthread_detach(thread);
	return true;
}
