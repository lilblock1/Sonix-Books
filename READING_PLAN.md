# Reading overhaul

Working baseline: `8dcb754968d6ad4b1f89a0aed98592e9a3cc0720`, tested by Noah
on his HiBy R1. Development branch: `reading-overhaul`.

Noah authorized a test firmware build after item 6, including items 1-6.
Items 7 and 8 are implemented. Item 9 adds common book formats and SD package updates
on `codex/common-book-formats`. Noah authorized completing it and checking the update
against internal storage limits. The device candidate remains gated by automated checks;
large reader engines, fonts and dictionaries must stay in the SD bundle.
Do not poll build status; Noah will report results. Component checks are allowed.
Preserve the working kernel/drivers and the firmware size/integrity safeguards.

1. [x] Library search by title/author, folder browsing, sorting (source and local checks; device validation pending).
2. [x] Per-book progress and New/Reading/Finished labels on the shelf (source and local checks; device validation pending).
3. [x] Recently read shelf for multiple books (12-book history; source and local checks; device validation pending).
4. [x] Reading-specific physical-button mapping, including optional volume-key page turns (source and local checks; device validation pending).
5. [x] Search inside books and jump to a result (source and local checks; device validation pending).
6. [x] SD-card font selection with preview and seven-family font pack (source and local checks; device validation pending).
7. [x] Named bookmarks and notes (source and automated checks; device validation pending).
8. [x] Offline dictionary lookup from the reader (source and automated checks; device validation pending).
9. [ ] Common book formats with reading controls, bookmarks and resume; source integrated,
   TXT/SD installer regressions passed; native engine and R1 candidate checks in progress.

Implementation constraints: 64 MB device RAM; bounded background work; keep
existing reading positions/bookmarks compatible. Distinguish verified tests
from first-device validation. Do not publish a stable release automatically.
Noah authorized an optional Wi-Fi Test channel and automatic prereleases only
after all automated checks pass; retain hardware_tested:false until tested.

Items 1–5 usage, limits and validation: [READING_LIBRARY.md](READING_LIBRARY.md).
Item 7 usage, persistence and validation: [BOOKMARKS_AND_NOTES.md](BOOKMARKS_AND_NOTES.md).
Item 8 usage, format and validation: [OFFLINE_DICTIONARY.md](OFFLINE_DICTIONARY.md).
Item 9 formats, limits and SD update behavior: [COMMON_BOOK_FORMATS.md](COMMON_BOOK_FORMATS.md).
