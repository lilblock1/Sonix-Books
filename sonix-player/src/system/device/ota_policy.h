#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdbool.h>

// The GitHub asset digest is mandatory. This is integrity verification against
// the configured GitHub release, not a signature or a hardware-compatibility proof.
bool ota_policy_digest(const char *digest);
bool ota_policy_asset(const char *repo, const char *stem, const char *url, long size);
bool ota_policy_battery(const char *percent);

#endif
