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

## Reading 0.2.0 candidate acceptance

The previous Sonix Books 0.1.0 image passed Noah's device test. This 0.2.0
candidate adds reading items 1-6 and needs its own test before publication.

- Search title, author and filename; browse folders and all five sorts.
- Open several books, restart, and check progress badges and the recent shelf.
- Test next/previous and optional volume page turns; holds must not seek music.
  Verify power/key-lock combinations and background audio.
- Find text in a long book; cancel a search, jump to a result, then reopen to
  confirm the saved location. Try a missing term and a damaged book.
- Extract the font pack to the card. Preview and apply several typefaces,
  including OpenDyslexic; check regular/bold/italic, all font sizes, spacing,
  vertical scrolling and paged mode. Confirm the selected font survives reboot.
- Close the reader, remove a selected custom font and reopen: Default should
  work. Restore the file and reopen to recover the saved typeface.
- Check that Stable ignores test prereleases and Test finds a newer numbered
  candidate. Cancel a download; check verified-download confirmation and low
  battery refusal. Switch back to Stable without an automatic downgrade.
- Read and search a large EPUB while playing audio. Repeat menu/preview/open/
  close cycles; check responsiveness, sleep/wake and Wi-Fi.

Record the exact candidate SHA-256 that passed. A successful Actions build
alone is not a hardware-test attestation or a published OTA update.

## Wi-Fi updates from GitHub

On a channel-enabled build, connect Wi-Fi and choose **Settings > System >
Update firmware > Via internet**. Choose **Stable** for hardware-tested releases
or **Test** to opt into experimental prereleases from lilblock1/Sonix-Books.
Stable is the default. The preference is remembered, and the Test choice shows
that hardware testing is pending before checking. Test also accepts newer
stable releases; neither channel selects a downgrade.

Only successful main-push candidates with all host checks, the MIPS/ABI build,
and firmware audits passing are automatically published as test prereleases.
They are numbered vA.B.C.N-test, where N is the build workflow number; the image
reports A.B.C.N. The exact same numbered image may later be published as a
hardware-tested stable release. An installed test build does not need to
reinstall identical bits from Stable. Raise the three-part source version for
the next stable feature series.

The updater checks the correct model filename, size and GitHub SHA-256 digest,
shows notes, downloads to a .part file and verifies it before renaming. After
download verification, a separate confirmation starts installation, with the
battery checked again. No background checking or unattended installation is
added. Test releases retain hardware_tested:false in the audit report. These
checks reduce avoidable errors but cannot prove that a test image will boot.

Sonix Books 0.1.0 has no Test channel and ignores prereleases. Install this
channel-enabled candidate once from SD; later test candidates can use Wi-Fi.

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
