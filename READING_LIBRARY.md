# Reading library — items 1 and 2

Source work on `reading-overhaul`; no firmware update has been built or
published for this feature. Keep the tested `main` firmware until the complete
nine-item reading list is ready for its combined build.

## Using the library

- Put EPUB files in `Ebook` on the SD card. Nested folders are supported.
- Tap a folder to open it; **Up** returns to its parent.
- Tap the search field and submit with the keyboard's search key. Searches
  match the EPUB title, author or filename across `Ebook` and its subfolders,
  regardless of the folder currently open. Matching is literal, with simple
  case folding for Latin, Greek and Cyrillic; CJK text matches literally.
- **Clear** (or **Up** while searching) returns to the folder you were browsing.
- The sort menu offers title A–Z, title Z–A, author A–Z, newest and oldest
  file modification date. Folders stay first and alphabetical. The sort choice
  survives restarts; titles and authors come from the book's metadata.
- Books without readable metadata retain their filename. Opening one still
  uses the existing reader's error handling.
- Home's **Continue Reading** and the existing bookmarks list resolve nested
  EPUB paths. Existing position and bookmark keys are unchanged.

## Progress on covers — item 2

Every book cover now has a status badge and progress bar:

- **New**: no valid saved reading position; opening a readable book changes it
  to **Reading 0%**.
- **Reading N%**: estimated progress at the saved position. Chapter file sizes
  weight the whole-book estimate; the current block and its text offset
  estimate progress within a chapter. It is not an exact word/page count.
- **Finished 100%**: advance once more after the final page, or tap **Finish
  book** at the bottom of the final chapter in scrolling mode. Reopening at
  that position preserves Finished. Moving to an earlier position changes it
  back to Reading. An estimate alone never marks a book Finished.

Older saved positions display **Reading** without a percentage or bar until
the book is reopened. Their original resume position is preserved. Folders
have no reading badge. Status is independent for books in different subfolders,
even when their filenames are identical.

Progress extends the existing `pos_<path hash>` value as
`chapter:block:offset|p1:percent:finished`. It adds no config entries and uses
the existing position-save/flush points. The three-number prefix also remains
readable by the old reader. Cover badges read only the in-memory config;
they do not parse chapters, start timers, or read book content from the card.

## Resource limits

The R1 has 64 MB RAM. Scans run on one background worker, cancel between files,
and retain at most 300 sorted entries. Traversal is limited to 10,000 entries
and 16 levels below the search root. Limits are shown in the library; browsing
a smaller folder or narrowing a query can reduce the result count. Browsing
also permits entering deeper folders directly. Hidden entries and symlinks
are skipped. Dot components and absolute paths cannot be entered via folder
navigation.

Metadata is cached under `.local/ebook-meta` on the SD card, keyed by path
and validated against file size and modification time. A corrupt, absent or
unwritable cache falls back to reading the EPUB. Scans do not load chapter text
or decode covers. Covers are requested near the viewport, released when they
leave its lookahead area, and can be requested again on return.

## Validation

Local checks completed:

- Progress tests: New/Reading/Finished transitions; save/reload; preservation of
  legacy positions and settings; compatible position prefixes; independent
  nested and long paths; completion/rewind behavior; malformed extensions and
  integer bounds; and progress within long paragraphs. Native Windows tests
  remove the test config before a changed-state flush to accommodate Windows
  CRT rename semantics; the deployed Linux atomic-save code is unchanged.
- Scanner tests: title/author/filename search, nested and empty folders, all
  five sorts, case folding, damaged-book fallback, cache hits/invalidation/
  corruption, unavailable cache, invalid paths, cancellation, nested resume
  resolution, deterministic 300-result limit and depth limit.
- Real compressed EPUB fixture with title `Pride & Prejudice`, author
  `Jane Austen`, and a different filename inside a subfolder: metadata search
  succeeded and the selected book's chapter opened successfully. The local
  Windows run used a test-only VirtualAlloc adapter for the EPUB arena.
- Changed C components compiled individually against LVGL 9.6 and Linux
  headers with warnings treated as errors. This is not a linked player or
  firmware build. Local MIPS libc headers were unavailable; the actual
  toolchain/link/size checks remain for the combined build.

The progress module, reader and shelf also compiled individually against the
Linux headers with warnings treated as errors. No linked player, firmware
update, release or version change was made for either item.

The POSIX symlink cases are included in the scanner tests for the future Linux
checks. They were not executed by the native Windows test run. On-device
layout, touch behavior, card-removal behavior, and memory use still need
validation with the combined firmware. No Actions run was dispatched or polled.

Run the scanner tests from `sonix-player` on Linux:

```sh
mkdir -p build_tests
gcc -std=c11 -Wall -Wextra -Werror -I. \
  tests/test_ebooklibrary.c src/system/ebook/ebooklibrary.c \
  -o build_tests/test_ebooklibrary
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
./build_tests/test_ebooklibrary "$fixture"
```

Run progress tests from `sonix-player` on Linux:

```sh
gcc -std=c11 -Wall -Wextra -Werror -I. \
  tests/test_ebookprogress.c src/system/ebook/ebookprogress.c \
  src/system/ebook/bookmarks.c src/system/core/config.c \
  -o build_tests/test_ebookprogress
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
./build_tests/test_ebookprogress "$fixture"
```

The existing checks workflow will run this test when it is eventually invoked
on a supported branch. Pushing `reading-overhaul` does not match its push
triggers and does not build an update.
