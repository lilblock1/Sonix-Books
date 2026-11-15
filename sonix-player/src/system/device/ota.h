#ifndef OTA_H
#define OTA_H

#include <stdbool.h>

// Firmware update over the network: the newest stable release on the
// project's GitHub page that carries this model's .upt, downloaded to the card
// under the name firmware.h looks for. Installing it is then the same step as
// an update from the card.
//
// The repository is "ota-repo" in system-info.json, "owner/name";
// lilblock1/Sonix-Books when the file does not say.
//
// Stable ignores drafts and prereleases. The optional Test channel also accepts
// vA.B.C.N-test prereleases, whose numeric build N must be newer than installed.
// Both channels retain model, size, digest and battery guards. No downgrade is
// selected automatically. The channel preference defaults to Stable.
//
// Both steps run on a worker thread, one at a time. The interface polls
// ota_state() and the rest; nothing here calls back into it.

typedef enum {
	OTA_IDLE = 0,
	OTA_CHECKING,
	OTA_DOWNLOADING,
	OTA_FINISHED, // ota_result() says how
} ota_state_t;

typedef enum {
	OTA_OK = 0,			 // check: a newer release is in ota_release(); download: the file is in place
	OTA_UP_TO_DATE,		 // check: nothing newer for this model
	OTA_NO_NETWORK,		 // Wi-Fi is not connected
	OTA_NO_MODEL,		 // system-info.json names no model this build knows
	OTA_CHECK_FAILED,	 // the release list could not be fetched or read
	OTA_NO_CARD,		 // no microSD mounted
	OTA_NO_SPACE,		 // not enough room on the card for the file
	OTA_DOWNLOAD_FAILED, // the connection failed or ended early, or the card refused a write
	OTA_CORRUPT,		 // the file arrived whole but its size or SHA-256 does not match the release
	OTA_CANCELLED,
} ota_result_t;

// The longest description kept, in bytes. GitHub allows 125,000 characters;
// a changelog that long would take the card a while to lay out.
#define OTA_NOTES_MAX (64 * 1024)

typedef struct {
	char tag[32];
	bool test_build;
	char name[128];		// the release title, the tag when it has none
	const char *notes;	// the release description, Markdown as written, cut at OTA_NOTES_MAX; never NULL
	long size;			// bytes
	char url[512];		// browser_download_url of the asset
	char sha256[65];	// required: releases without GitHub's asset digest are refused
	bool packages_only;
	long packages_size;
	char packages_url[512], packages_sha256[65];
} ota_release_t;

// Starts looking for a release. False when a check or a download is already
// running.
bool ota_check_start(void);

bool ota_test_channel(void);
bool ota_set_test_channel(bool enabled);

// Downloads the release found by the last successful check. False when there
// is none, or something is already running.
bool ota_download_start(void);

// Asks the running step to stop. It ends in OTA_FINISHED with OTA_CANCELLED,
// unless it had already finished.
void ota_cancel(void);

ota_state_t ota_state(void);
ota_result_t ota_result(void);

// The release the last check found. Only meaningful after a check that
// finished with OTA_OK, and unchanged until the next check starts.
const ota_release_t *ota_release(void);

// Bytes written so far and bytes expected, for the progress bar.
void ota_progress(long *done, long *total);

// Back to OTA_IDLE once the interface has read a finished result.
void ota_acknowledge(void);

// True while a check or a download is in progress: power.c keeps the device
// and the Wi-Fi radio awake for it.
bool ota_busy(void);

// The version installed, as system-info.json gives it; "" when unknown.
const char *ota_installed_version(void);

#endif /* OTA_H */
