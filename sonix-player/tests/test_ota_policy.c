#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "src/system/device/ota_policy.h"

int main(void) {
	char digest[80] = "sha256:";
	memset(digest + 7, 'a', 64);
	digest[71] = '\0';
	assert(ota_policy_digest(digest));
	digest[24] = 'G';
	assert(!ota_policy_digest(digest));
	digest[24] = 'F';
	assert(ota_policy_digest(digest));
	assert(!ota_policy_digest(NULL));
	assert(!ota_policy_digest(""));
	assert(!ota_policy_digest("sha256:123"));
	assert(!ota_policy_digest("md5:01234567890123456789012345678901"));
	const char *repo = "lilblock1/Sonix-Books";
	const char *valid = "https://github.com/lilblock1/Sonix-Books/releases/download/v0.1.1/r1.upt";
	assert(ota_policy_asset(repo, "r1", valid, 47996928));
	assert(!ota_policy_asset(repo, "r3proii", valid, 47996928));
	assert(!ota_policy_asset("Jepl4r/sonix-player", "r1", valid, 47996928));
	assert(!ota_policy_asset(repo, "r1", "https://github.com.evil.example/lilblock1/Sonix-Books/releases/download/v1/r1.upt", 100));
	assert(!ota_policy_asset(repo, "r1", "http://github.com/lilblock1/Sonix-Books/releases/download/v1/r1.upt", 100));
	assert(!ota_policy_asset(repo, "r1", "https://github.com/lilblock1/Sonix-Books/releases/download/v1/r1.upt?x=1", 100));
	assert(!ota_policy_asset(repo, "r1", "https://github.com/lilblock1/Sonix-Books/releases/download/v1/other/r1.upt", 100));
	assert(!ota_policy_asset(repo, "r1", valid, 0));
	assert(!ota_policy_asset(repo, "r1", valid, 129L * 1024 * 1024));
	assert(!ota_policy_asset(repo, "r1", NULL, 100));
	assert(ota_policy_battery("50"));
	assert(ota_policy_battery("100"));
	assert(!ota_policy_battery("49"));
	assert(!ota_policy_battery("101"));
	assert(!ota_policy_battery("!!"));
	assert(!ota_policy_battery("50junk"));
	assert(!ota_policy_battery(NULL));
	puts("OTA policy: digest, model/repository URL, size and battery checks passed");
	return 0;
}
