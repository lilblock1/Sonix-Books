#include "src/system/device/sdpackages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/system/image/miniz/miniz_tinfl.h"
#include "src/system/image/miniz/tinfl_impl.inc"
static uint64_t made, cancel_at;
static bool cancel(void *user) { (void)user; return cancel_at && made >= cancel_at; }
static void progress(uint64_t done, uint64_t total, void *user) { (void)total; (void)user; made = done; }
int main(int argc, char **argv) {
	if (argc != 6 && argc != 7) return 2;
	cancel_at = strtoull(argv[5], NULL, 10);
	unsigned category = argc == 7 ? (unsigned)strtoul(argv[6], NULL, 10) : 0;
	sdpackages_result_t result = argc == 7 ? sdpackages_install_extra(argv[1], category, argv[2], argv[3], argv[4], cancel, progress, NULL)
		: sdpackages_install(argv[1], argv[2], argv[3], argv[4], cancel, progress, NULL);
	char version[SDPACKAGES_VERSION], generation[SDPACKAGES_VERSION];
	bool active = argc == 7 ? sdpackages_component_version(argv[1], category, version) : sdpackages_generation(argv[1], generation, version);
	printf("%d %s\n", result, active ? version : "none");
	return 0;
}
