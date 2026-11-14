# Reading overhaul

Working baseline: `8dcb754968d6ad4b1f89a0aed98592e9a3cc0720`, tested by Noah
on his HiBy R1. Development branch: `reading-overhaul`.

No new firmware update build until all nine items below are complete.
Do not poll build status; Noah will report results. Component checks are allowed.
Preserve the working kernel/drivers and the firmware size/integrity safeguards.

1. [x] Library search by title/author, folder browsing, sorting (source and local checks; device validation pending).
2. [x] Per-book progress and New/Reading/Finished labels on the shelf (source and local checks; device validation pending).
3. [ ] Recently read shelf for multiple books.
4. [ ] Reading-specific physical-button mapping, including optional volume-key page turns.
5. [ ] Search inside books and jump to a result.
6. [ ] SD-card font selection with preview.
7. [ ] Named bookmarks and notes.
8. [ ] Offline dictionary lookup from the reader.
9. [ ] TXT books with reading controls, bookmarks and resume.

Implementation constraints: 64 MB device RAM; bounded background work; keep
existing reading positions/bookmarks compatible. Distinguish verified tests
from first-device validation. Do not publish a stable release automatically.

Items 1 and 2 usage, limits and validation: [READING_LIBRARY.md](READING_LIBRARY.md).
