# WaveCore OS (working name), phase 1

Experimental independent Linux userspace for HiBy R1. Existing Sonix Books Update 13 remains the known working reference; the branch name sonix-os is an internal development name, not the product name.

## Build
Run the WaveCore foundation workflow manually from this branch. It produces a Buildroot rootfs tarball and compatibility inventory only. Never install this artifact with the R1 updater. No kernel, bootloader, recovery, flash partition, SD card or user data is modified by this workflow.

The first profile uses MIPS32r2 little endian, glibc, BusyBox init, 4.4-series headers, devtmpfs/mdev, no network daemons, and no graphical desktop. The floating-point ABI is provisional until compared with the Update 13 player and original shared libraries. The rootfs is deliberately incomplete: Sonix Books UI, all R1 drivers, proprietary radio firmware, ALSA integration, storage/init/recovery, and compatibility with existing game binaries must be validated before a real device image exists.

## Roadmap
1. Minimal userspace and baseline ABI/hardware inventory (this stage).
2. Preserve the proven kernel, module load order, device nodes, bootloader and stock recovery. Port hardware services and attach the player UI; prove display, touch, audio, battery, SD, Wi-Fi and Bluetooth.
3. Add isolated game worker process and bounded IPC, then benchmark FPS, p95 frame times, available memory, CPU and battery against Update 13 on the same ROMs and settings.
4. Safe SD-based development boot if independently verified possible, then a separate test updater and exact partition-size verification. Never overwrite the current stable/Test channel.

Do not claim performance improvements until measured on hardware. Buildroot and the independent userspace have their own licenses; produce legal-info before distributing any binaries.
