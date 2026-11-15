# Sonix Game Store (HiBy R1 only)

The device fetches release metadata from `https://api.github.com/repos/lilblock1/Sonix-Books/releases/tags/sonix-game-store-v1`. The catalog is a separate non-latest prerelease; it is not a firmware release and its tag is intentionally rejected by the firmware updater. The device uses GitHub's per-asset SHA-256, exact official asset URLs, and a fixed R1 content ABI. A new game version adds one new asset; the store compares numeric versions and shows the newest compatible one. Do not overwrite published asset names.

## Asset contract

- Asset name: `sonix-game-v1-<id>-v<major>.<minor>.<patch>.spk`
- IDs: lowercase letters/digits and hyphens, maximum 31 characters, no leading/trailing hyphen.
- Package: SONIXPK1 streaming archive, schema 1, target r1; member names must begin `Games/<id>/`.
- Installation: `.sonix/game-<id>/<generation>/Games/<id>/` on the microSD card. Every title has separate active/previous generation pointers. A failed install leaves the previous active version intact.
- User ROMs and saves live outside managed generations. Never distribute unauthorized game ROMs or BIOS images.
- Planned native engine interface: libretro v1 (little-endian MIPS32r2, ELF32, verified stock-compatible glibc imports). `core.so` and any redistributable content/licenses live in the package. Core loading and input/audio/video integration are a separate required milestone; installation does not execute a core.
- Do not publish a playable-game claim before testing the launcher and the game on R1 hardware. The 64 MB RAM limit and potentially noexec SD mount must be respected. Never modify mount flags, kernel, system files or ADB to make a core run.

## Independent publishing

Develop cores on this content branch. Each directory `game-store/cores/<id>` has a Makefile, source, licenses and a version file. The independent publishing workflow will compile one selected core, check its MIPS ELF and dependencies, verify the package using the real SD installer, and upload only that asset to the catalog. It is gated on an explicit publishing request. A new core version does not change the firmware version and must not rebuild or release `.upt` files.

Initial priorities: a native 2048 puzzle, QuickNES, gpSP, then Doom/Freedoom and other lightweight cores. Core runtime and hardware validation are pending. The catalog bootstrap does not publish unfinished binaries.
