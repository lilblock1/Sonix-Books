#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdbool.h>

bool ota_policy_package(const char *repo, const char *tag, const char *url, long size);

// The GitHub asset digest is mandatory. This is integrity verification against
// the configured GitHub release, not a signature or a hardware-compatibility proof.
bool ota_policy_digest(const char *digest);
bool ota_policy_asset(const char *repo, const char *stem, const char *url, long size);
bool ota_policy_battery(const char *percent);

// Four numeric parts support monotonically numbered test candidates. A test
// release must be a GitHub prerelease tagged vA.B.C.N-test; Stable ignores it.
bool ota_policy_version(const char *text, long out[4]);
int ota_policy_version_cmp(const long a[4], const long b[4]);
bool ota_policy_release(bool test_channel, bool draft, bool prerelease, const char *tag);

#endif
