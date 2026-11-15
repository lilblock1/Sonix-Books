# Reading library — items 1–5

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

## Recently Read — item 3

Tap **Recently read** at the top of the library to switch to the 12 most
recently opened books, ordered newest first. Reopening a book moves it to the
front without duplicates. Its cover, reading badge and saved position work
as in the main library. **Back to library** or **Up** returns to the folder you
were browsing. Closing the reader returns to the same view and refreshes it.

Search in this view filters recent titles, authors and filenames while keeping
recent order; **Clear** removes the filter. The regular sort control is hidden
here and retains its previous choice for the main library. An empty recent
view has a direct way back to browse books.

Home's **Continue Reading** opens the first available recent book on the
current card, including subfolders. Missing files are hidden from the view;
their hashes remain remembered so that temporarily removing a card does not
erase history. No file path loaded from the settings is opened directly.
Resolution uses the same bounded traversal and symlink rules as library search.

History uses two fixed config values containing short path hashes and retains
legacy last_book for compatibility. An existing last_book is migrated into the
list; using an older firmware and returning also preserves its last selection.
No clock, per-book timestamps, background polling, or extra page-turn writes
are needed. Config is read only on the UI thread; the worker gets an immutable
history snapshot and parses metadata only for remembered files.

## Reading buttons — item 4

Open the Books gear button to reach **Reading settings**, then choose a
**Volume button action**:

| Choice | Volume up | Volume down |
| --- | --- | --- |
| Volume (default) | Usual configured action | Usual configured action |
| Up: Back | Previous page | Next page |
| Up: Next | Next page | Previous page |

While a book is open, the case's **Next** and **Previous** buttons turn pages,
and **Play/Pause** opens or closes the reading menu. The R1 has no separate
Previous button; **Up: Back** provides one through volume up. These reading
bindings take priority over the case's music remapping and double-click setup.
They apply to subsequent presses only while reading; music settings are retained
for use after closing the reader.

A held reading key produces exactly one action on release. It does not repeatedly
flip pages or seek background music. In scrolling mode, page buttons move about
90% of a screen and cross chapter boundaries. Next at the end of the final
chapter marks the book Finished, as it does after the final paginated page.
Page actions are consumed while the reader menu or a page animation is visible.

Power and enabled power/volume combinations keep their existing behavior.
A screenshot or lock combination consumes the volume gesture without also
turning a page. Existing screen/key-lock rules still apply. Headphone and
Bluetooth volume controls retain their normal volume behavior.

Reading commands carry their reader session through the input queue. A release
or queued action from a closed book is discarded, including when another book
has since opened; it never becomes a music command. Pending case-button music
holds/double-clicks spanning a reader context change are cancelled. Input threads
read atomic context snapshots and post actions through the existing GUI bridge;
all widget access remains on the UI thread. The setting uses one ebook-config
key (volume_pages), read on the UI thread and preserved across restarts.

## Find in book — item 5

Open the reader menu with Play/Pause or a centre long press, then tap **Find in
book**. Type text and press the keyboard's Search key. Results show chapter
numbers and excerpts; tap one to jump to the matching text. **Back** or
Play/Pause cancels and restores the original place, including the scroll
position in scrolling mode. Closing a book cancels search without restoring a
chapter in a screen that is no longer active.

Search is literal and case insensitive using the library's shared simple
Unicode folding. Whitespace runs are treated as a single space. Phrases can
cross bold/italic span boundaries within a paragraph, heading, quote or list
item. Matches do not cross paragraph/block boundaries. Image filenames and
rules are excluded. There is no regex, fuzzy matching, or full Unicode
normalization. Search result offsets refer to original book bytes rather than
an altered or normalized copy.

Searching does not change reading progress, bookmarks or history. A selected
result becomes the new reading position through the ordinary reader save path.
If a result cannot be loaded, the reader tries to restore its original chapter.
An unavailable book uses the existing error/return path.

For the 64 MB R1, the reader releases its displayed chapter and images before
building the search overlay. A single background worker opens its own handle
and parses one chapter at a time; it does not decode images or hold the whole
book. Closing search waits asynchronously until the worker has closed its book
before restoring the reader chapter. New queries replace pending requests;
late results from cancelled queries are discarded. Physical page keys are
consumed while Find is open, and fresh input sessions discard queued commands
when focus returns to the reader.

Search retains at most 100 hits with 192-byte UTF-8 excerpts. Queries are
bounded to 256 bytes/96 normalized characters (the keyboard limits input to
60 characters). A scan examines at most 64 MiB of chapter content and 64 MiB of
visible text, reporting limits rather than claiming an exhaustive search.
Unreadable chapters are skipped and counted. Cancellation is checked between
chapters/spans and every 1024 decoded characters; a chapter parser already in
progress finishes before cancellation can release it. The UI shows chapter
progress and remains available for cancelling.

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

- Search tests: phrases across styled spans, whitespace folding, Latin/Greek/
  Cyrillic casing and CJK matches, literal symbols, original byte offsets,
  overlapping results, bounded UTF-8 excerpts, empty/long queries, skipped
  chapters, cancellation, scan limits and the 100-result cap.
- A generated compressed EPUB exercises the real ZIP/OPF/XHTML engine, entity
  decoding, styled-phrase matching, isolation from an existing reader handle,
  and loading every returned chapter/block/byte offset. The local Windows run
  used the previously documented test-only virtual-memory arena adapter.
- Library scanner regressions passed after sharing the bounded UTF-8 decoder.
  The search model, shared decoder, Find overlay and reader compiled with Linux
  headers/LVGL 9.6 and warnings treated as errors. Full UI and device validation
  remains deferred to the combined build.

- Reading-button tests: default case bindings; both volume-page directions;
  settings persistence/defaults; one action after arbitrarily repeated holds;
  duplicate releases; power-combination suppression; simultaneous keys/nodes;
  headset exclusion; queued actions after closing/reopening; mode changes while
  held; missing-release recovery; and preservation of reading settings/positions.
  Native Windows config retries use the same test-only rename accommodation
  described below.
- The full device input file, GUI bridge, reader, reading settings and routing
  module compiled individually with Linux headers, LVGL 9.6 and the pinned
  ALSA 1.0.26 headers; warnings were treated as errors. These are component
  checks, not a linked player or firmware build.

- Existing reading-history regression tests also passed locally with Windows
  filesystem adapters (including paths longer than the config value limit).
- Recent-history tests: ordering across save/reload, reopening and deduplication,
  the 12-book cap, two fixed storage values, migration from last_book, independent
  nested paths, immutable worker snapshots, long filenames and malformed data.
  Native Windows tests retry dirty writes after removing the test config to
  account for CRT rename semantics; the deployed config writer is unchanged.
- Library tests also cover recent order, filtering, cancellation, skipping absent
  files, selecting the first available recent file, and reading metadata only
  for remembered books.

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

The progress/history modules, reader, library scanner and shelf also compiled individually against the
Linux headers with warnings treated as errors. Those component checks did not link the player or create firmware. Version
0.2.0 now identifies the authorized combined test candidate for items 1-6.

The POSIX symlink cases are included in the scanner tests for the future Linux
checks. They were not executed by the native Windows test run. On-device
layout, touch behavior, card-removal behavior, physical key timing, power/key-lock
combinations, audio during reading, and memory use still need
validation with the combined firmware. No Actions run was dispatched or polled.

Run the scanner tests from `sonix-player` on Linux:

```sh
mkdir -p build_tests
gcc -std=c11 -Wall -Wextra -Werror -I. \
  tests/test_ebooklibrary.c src/system/ebook/ebooklibrary.c src/system/ebook/ebooktext.c \
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

Run recent-history tests from sonix-player on Linux:

~~~sh
gcc -std=c11 -Wall -Wextra -Werror -I. \
  tests/test_ebookrecents.c src/system/ebook/ebookhistory.c \
  src/system/ebook/bookmarks.c src/system/core/config.c \
  -o build_tests/test_ebookrecents
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
./build_tests/test_ebookrecents "$fixture"
~~~

Run button tests from sonix-player on Linux:

~~~sh
gcc -std=c11 -Wall -Wextra -Werror -I. \
  tests/test_readingkeys.c src/system/input/readingkeys.c \
  src/system/core/config.c -o build_tests/test_readingkeys
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
./build_tests/test_readingkeys "$fixture"
~~~

Run in-book search tests from sonix-player on Linux:

~~~sh
gcc -std=c11 -Wall -Wextra -Werror -I. \
  tests/test_ebooksearch.c src/system/ebook/ebooksearch.c \
  src/system/ebook/ebooktext.c -o build_tests/test_ebooksearch
./build_tests/test_ebooksearch
python3 tests/test_ebooksearch_epub.py
~~~

The checks workflow runs these tests on main. The authorized main push for
items 1-6 also triggers the R1 candidate build. All checks must pass before automatic
publication as an experimental prerelease; Stable still requires device testing.

## SD-card font selection (item 6)

Extract [Sonix-Books-Fonts.zip](extras/reading-fonts/Sonix-Books-Fonts.zip) to the
SD-card root, merging its Ebook directory. In an open book, open Font settings,
then Typeface. Select a font to preview it and choose Apply to save it. Default
restores the existing automatic Bookerly/Japanese selection.

The pack contains Atkinson Hyperlegible, Source Serif 4, Source Sans 3, Source
Code Pro, OpenDyslexic, OpenDyslexic Alta and OpenDyslexic Mono. Compare them for
your own reading comfort. The pack stays on the card, outside the firmware.
See [font pack details and licenses](extras/reading-fonts/README.md).

Additional TTF/OTF files can go directly in Ebook/Fonts, or in a family directory
with Regular.ttf (or .otf) and optional Italic, Bold and BoldItalic files. The
picker supports 32 choices, scans at most 256 directory entries and rejects
files over 16 MiB. Paths and font container bounds are checked; symbolic links
are rejected. The preview uses a separate face and changes nothing until Apply.
Missing styles use an available face. Missing saved fonts use Default without
erasing the selection, so reinserting the card restores it on the next open.

Reader labels detach before replacing or resizing their font faces. Unit tests
cover catalog/path bounds, invalid previews, persistence, style fallback, saved
font removal/recovery and balanced face lifetimes using a fake FreeType backend.
Separately, every actual font file passed pinned checksum, SFNT bounds and
license-notice checks and rasterized with Pillow/FreeType locally. The device
still needs font preview, scrolling, card removal and memory checks.
