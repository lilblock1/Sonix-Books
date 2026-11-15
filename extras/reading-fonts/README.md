# Sonix Books SD-card fonts

Unzip `Sonix-Books-Fonts.zip` onto the SD card's root, merging its `Ebook`
folder. The fonts belong in `Ebook/Fonts`, outside the firmware image.

With the updated reader, open a book, open its reading menu, choose Font
settings, then **Typeface**. Tap a family to preview it and choose **Apply font**.
**Default** restores the automatic Bookerly/Japanese selection. Reopen the
font list after copying files; a restart is not required.

Included families:

| Family | Styles | Upstream license |
| --- | --- | --- |
| Atkinson Hyperlegible | Regular, italic, bold, bold italic | SIL OFL 1.1 |
| Source Serif 4 | Regular, italic, bold, bold italic | SIL OFL 1.1 |
| Source Sans 3 | Regular, italic, bold, bold italic | SIL OFL 1.1 |
| Source Code Pro | Regular, italic, bold, bold italic | SIL OFL 1.1 |
| OpenDyslexic | Regular, italic, bold, bold italic | CC BY 3.0 changes and inherited Bitstream Vera license |
| OpenDyslexic Alta | Regular, italic, bold, bold italic | CC BY 3.0 changes and inherited Bitstream Vera license |
| OpenDyslexic Mono | Regular | CC BY 3.0 changes and inherited Bitstream Vera license |

The OpenDyslexic variants are included in response to the requested dyslexia
font choice. Use the previews to compare which face feels comfortable. Mono
has only a regular face; the reader uses it for missing styles.

Font bytes are unmodified. Only filenames have been normalized to `Regular.ttf`,
`Italic.ttf`, `Bold.ttf` and `BoldItalic.ttf` for the picker. Each family contains
its copyright notices and license. The classic OpenDyslexic files in this pack
carry the older CC BY/Bitstream notices embedded by their publisher; they are
not relabeled as a newer OFL version.

`font-manifest.json` records pinned upstream commits, source URLs, sizes and
SHA-256 hashes. `Sonix-Books-Fonts.zip.sha256` verifies the ZIP itself. Keep the
license notices with redistributed copies. The upstream authors are credited
in the manifest and license files; this pack is not an endorsement by them.

You can also install your own TTF or OTF files directly inside `Ebook/Fonts`.
For a complete family, make a subfolder with `Regular.ttf` (or `Regular.otf`)
and optional `Italic`, `Bold` and `BoldItalic` files. Keep the same base names
and use `.ttf` or `.otf`. The picker supports up to 32 choices and rejects font
files larger than 16 MiB or invalid font containers. Missing or unreadable
saved fonts fall back to Default without erasing the saved selection.
