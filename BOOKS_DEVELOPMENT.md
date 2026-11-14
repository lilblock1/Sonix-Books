# Sonix Books development

This fork is based on Jepl4r/sonix-player at
`fe94fe796d93c6efa50d1107a734864c9c6a03d5`. The primary hardware target is the
HiBy R1. The inherited R3 Pro II code and resource set are retained, but that
does not constitute new hardware validation for either player.

## First milestone

The existing EPUB and Game Boy engines are retained. This milestone changes
navigation, adds persistent last-book discovery, gives short transport presses
to the active book or game, remembers the GB/GBC library selection, and isolates
online updates to this fork. Long-press audio seeking remains upstream behavior.

Reading history stores one path hash in `ebook_config.ini`; existing position
and bookmark keys are unchanged. No full filename is stored in the config's
128-byte value field, and resuming scans only regular EPUBs in the current
card's `Ebook` directory. There is no eager parsing of all books at startup.
If a book was renamed, removed, or is on a different card, Continue reading
opens the shelf. The current shelf limit remains 300 books.

## Validation

`.github/workflows/books-build.yml` runs:

1. C tests for persistent history, existing-state preservation, long filenames,
   missing cards and files, short buffers, non-EPUBs, and directories.
2. Checks of both resource sets, all language files, and the update destination.
3. A complete Linux simulator build from the real application source.
4. A headless startup smoke test and a Linux simulator artifact upload.

These checks do not prove touchscreen ergonomics, physical-button behavior,
audio output, power management, firmware packing, or device boot. Test those
on an R1 before publishing a firmware release. The simulator is not firmware.

## Device build

Use the upstream target build instructions in README.md (`make target`), then
the firmware packer with the matching R1 base image. Preserve the model identity
`HiBy R1` and output name `r1.upt`. The included kernel/modules and base image
need checking against the actual board revision before flashing, especially
newer wireless hardware. No device kernel or partition changes are introduced
by this milestone.

## Next stages

- Reader: searchable library, chapter text search, and richer progress metadata.
- Reader memory: bound decoded shelf-cover retention for large libraries.
- Games: quick-resume UX and control-layout improvements, measured on the R1.
- Input: an activity-aware raw button layer so long presses also belong to the
  current activity instead of the upstream audio-seeking path.

Additional ebook formats and emulator systems need their own memory and
performance assessment. They are not claimed as supported by this update.
