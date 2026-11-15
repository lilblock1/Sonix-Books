# Offline dictionary lookup — item 8

Development branch: `codex/offline-dictionary`, following item 7. This step
adds source, checks and a separate dictionary pack; it does not publish R1
firmware.

## On the R1

On Windows, extract **Sonix-Books-Dictionary.zip** and copy/merge its **Ebook**
folder into the SD-card root. The English data file is then
`Ebook/Dictionaries/English.sxd`. Keep the accompanying license and source
notices with it. The pack is distributed as the **sonix-books-offline-dictionary**
check artifact and is attached separately to future test firmware releases.

With a firmware containing item 8, open a book, open its menu with Play/Pause
or a centre long press, and tap **Dictionary**. Type a word or phrase and press
**Lookup**. Tap the text field to show the keyboard again. **Clear** starts a
new query. Definitions scroll with the keyboard hidden. **Back** or Play/Pause
returns to the same reading position, including the current scroll position.

Lookup is entered through the reader menu; this version does not change the
reader's long-press gesture into word selection. No network request, new
reading-position save, bookmark change or history entry is made by lookup.
Page keys are consumed while Dictionary is open, and queued page presses are
invalidated when entering/leaving it. Leaving the reader cancels lookup.

The English pack derives definitions and part-of-speech labels from Princeton
WordNet 3.0. It includes explicit irregular-form aliases from its exception
lists, such as **went** → **go**. It is a headword dictionary, without automatic
stemming, translation, pronunciation or spell correction. Try a base form when
an inflected form has no entry. Other dictionaries may be selected in the
selector; **More dictionaries** reaches the remaining choices. The selection
is retained in memory without adding settings writes.

## Other dictionaries and limits

Convert a UTF-8, two-column TSV file (headword TAB definition) with Python 3
on Windows or Linux:

```sh
python sonix-player/tools/dictionary_pack.py input.tsv MyDictionary.sxd
```

Copy the result directly into `Ebook/Dictionaries`. Standard CSV-style quoting
supports multiline definitions and embedded tabs. Duplicate normalized words
merge distinct definitions. This version reads **.sxd**, not raw StarDict,
DICT, JSON or compressed dictionary files. Only redistribute data you have
permission to redistribute, keeping its license and attribution.

Normalization matches the library's simple Latin/Greek/Cyrillic case folding,
collapses spaces/underscores, strips surrounding quotation/punctuation marks,
and handles curly apostrophes and common Unicode hyphens. It is not full Unicode
case folding or NFC normalization. A normalized headword is at most 95 UTF-8
bytes; the input is at most 255 bytes. Custom definitions may contain up to
64 KiB, with at most the first 8 KiB displayed per lookup; clipping is marked
and never ends mid-character.

The picker retains at most 8 dictionaries, examines at most 64 directory
entries, and only uses flat filenames shorter than 64 bytes. Dictionary files
are at most 128 MiB and contain at most 500,000 entries. Symlinks, special files,
hidden files and traversal IDs are rejected. Runtime validates the header,
visited index records, definition offsets/lengths and displayed UTF-8; malformed
or missing files produce a message rather than text from a prior lookup.

## Format, resources and provenance

`SNXDICT1` is a portable little-endian format with a 16-byte header
(`magic[8], count:u32, record_size:u32`) and fixed 104-byte records
(`NUL-padded UTF-8 word[96], definition_offset:u32, definition_size:u32`).
The index is sorted by normalized UTF-8 bytes. Definitions follow the index as
plain UTF-8. The builder verifies the entire index and all definitions before
atomic replacement. Runtime binary search touches at most 19 records, keeping
the dictionary on disk; it does not map or cache the full index/database.

One lazy background worker with a 128 KiB stack owns dictionary I/O. Pending
requests replace earlier pending requests, running work checks cancellation,
and generation tags discard late results. All handles are closed by that
worker before returning a result. There is no shared EPUB handle, chapter
parser, font rasterizer, background polling when idle, or on-device indexing.

`build_dictionary_pack.py` downloads the official WordNet archive, requires
its pinned SHA-256, reads only named regular archive members without extraction,
preserves the complete original license, and creates a deterministic zip with
source, checksum, citation and modification details. The archive checksum is
also recorded in Homebrew's WordNet recipe. The English data remains outside
the firmware image and its size checks.

Sources:

- https://wordnet.princeton.edu/
- https://wordnet.princeton.edu/license-and-commercial-use
- https://wordnet.princeton.edu/documentation/wndb5wn
- https://wordnetcode.princeton.edu/3.0/WordNet-3.0.tar.bz2
- https://github.com/Homebrew/homebrew-core/blob/master/Formula/w/wordnet.rb

## Validation

Converter tests and a C reader integration test cover exact disk-format
interoperability, 10,000 indexed lookups with a probe bound, Unicode and
punctuation normalization, duplicate definitions, multiline TSV, byte limits,
definition clipping, malformed/truncated records, offset overflows, traversal,
links/special files, missing/card-removed roots, catalog limits and cancellation.
The separate English pack job verifies the pinned archive and every generated
record. The full host application compile/startup check covers integration.

These checks passed in [run 43752549453](https://github.com/lilblock1/Sonix-Books/actions/runs/43752549453),
including the full host link/startup and existing reading/font/update regressions.
The verified English pack contains 151,366 headword/irregular-form entries.

R1 touch layout, long-entry scrolling, card removal during physical I/O,
keyboard/page-key timing, audio during lookup and memory use still need
on-device validation in a later combined firmware build.
