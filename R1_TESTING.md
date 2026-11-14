# R1 candidate builds and Wi-Fi updates

There is no guarantee against a brick. A valid image can still fail because of
hardware differences, flash failure, a bug, or loss of power while writing.
This device does not have a proven A/B rollback mechanism in this project.

## Build safeguards

The R1 device workflow compiles the actual MIPS executable and checks that it
does not ask for glibc newer than the device's 2.22. It then starts from the
published **Sonix R1 1.3.0** image, pinned by SHA-256:

`7617a76b212044ea0a3b533eb74cd3d04c28100cc3a08fd058834ef8ba6c4588`

The candidate builder verifies every original chunk and whole-image MD5,
checks the extracted device identity, and replaces only the player, language
files and Sonix version/update metadata. It does not overlay kernels, drivers,
libraries, boot scripts, bootloaders, recovery components or partition layouts.
It refuses a root filesystem larger than the known-working base image rather
than guessing the size of the device partition. It reopens the final `.upt`
and verifies its chunks and kernel again.

The target build uses `-Os` for UI/control code to reduce the flash footprint.
The existing audio, decoder, emulator and software-rendering hot paths retain
`-O3`. Host simulator flags are unchanged; check real-device responsiveness as
part of the acceptance checklist.

**Preserving kernel bytes does not mean the kernel partition is never written.**
The standard update manifest still carries the original kernel; the updater
can rewrite it. A power interruption is therefore still a risk.

The build artifact is a development candidate, not a stable release. If a size
or structural check fails, do not flash intermediate files. A report alone is
not an installable image.

## Before the first installation

1. Read the installed Sonix version and confirm it is a standard R1 build.
   Working Wi-Fi, touchscreen and buttons should be established before changing
   anything. If it is not Sonix 1.3.0, stop and check the base first.
2. Keep the exact working R1 installer and a separate backup of card contents
   and important settings. The pinned upstream installer is available at
   https://github.com/Jepl4r/sonix-player/releases/download/1.3.0/r1.upt.
3. Establish the device recovery procedure before inserting any new update.
   HiBy's button section documents Volume Up held at power-on for forced
   upgrade mode; another section of its manual describes different buttons,
   so do not treat an untested combination as guaranteed recovery. With an
   update file on the card, entering forced upgrade mode can start flashing
   automatically. Do not use a new candidate to test that key combination.
4. Charge to at least 50%, keep power stable, verify the candidate checksum,
   and do not interrupt an update. The new application refuses installation
   below 50% or with an unreadable battery value. Existing upstream Sonix does
   not gain this guard until the first Sonix Books installation.

## Device acceptance checklist

Check cold boot, touch, physical buttons, EPUB open/page/close/resume/bookmark,
reading themes, GB/GBC start/pause/save/load/exit, Flappy Bird, local music,
Wi-Fi, sleep/wake, charging, USB file transfer and return to the main menu.
Test with the actual card and filenames. Keep the audit report and SHA-256 of
the exact image tested. Simulator tests do not replace these checks.

## Wi-Fi updates from GitHub

After the first Sonix Books installation, connect Wi-Fi and choose
**Settings > System > Update firmware > Via internet**. The updater reads
`lilblock1/Sonix-Books` Releases, ignores drafts and prereleases, selects a
newer version with the correct device filename, shows its notes, downloads to
a `.part` file, and requires the GitHub SHA-256 digest before making it an
installable update. It does not silently install in the background.

Existing upstream Sonix still looks at Jepl4r's repository; the first install
of this fork normally uses an SD card. Merely creating a GitHub fork does not
redirect the firmware already on the device.

To publish after device testing, run **Publish tested R1 Wi-Fi update** in
GitHub Actions. Supply the successful candidate run ID, matching version tag,
the tested image's SHA-256, and the hardware-test attestation. The workflow
checks provenance, version, model, size, audit result and exact image hash
before creating the stable Release. The attestation is a human test record,
not proof from an automated hardware rig.

Sources:

- HiBy R1 manual: https://guide.hiby.com/en/docs/products/audio_player/hiby_r1/guide/
- Community recovery guide: https://github.com/hiby-modding/hiby_os_crack/blob/main/guides/UNBRICKING.md

Boot-ROM recovery uses different tools and device-specific recovery images.
It can erase settings and should not be attempted as an automatic fallback.
