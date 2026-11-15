# Common book formats and SD packages

Books open directly on the HiBy R1. No Windows conversion step is required.

| Files | Reader behavior |
| --- | --- |
| EPUB | Existing EPUB renderer, contents, styles and illustrations |
| TXT, TEXT | UTF-8, UTF-16 with BOM, or Windows-1252; bounded 64 KiB sections |
| MOBI, AZW, AZW3, PRC | DRM-free Kindle/Palm books imported on the R1 into an EPUB cache on the SD card |
| PDF, XPS, OXPS | Original page layout rendered one page at a time |
| CBZ, CBR, CBT | Comic archive pages rendered one page at a time |
| FB2, FBZ | FictionBook and zipped FictionBook |
| HTML, HTM, XHTML, MD, MARKDOWN | Text reflow with basic emphasis |
| DOCX, ODT, RTF | Text reflow; complex tables, embedded images and Office layout are not reproduced |

Password-protected/DRM books, Kindle Print Replica/KFX, DjVu and legacy binary
DOC are unsupported. A renamed extension does not convert a file. Scanned PDF
pages can be viewed but do not gain searchable text or OCR.

The library, recent books, bookmarks, notes, search and resume use the original
book path. Kindle imports do not change that identity. Fixed-page search jumps
to the matching page. Hold a PDF/comic page to open zoom; drag to pan and use
the minus/plus controls. Close or press Play/Pause to return to reading.

## Storage and update behavior

The firmware contains the small reader frontend and TXT backend. MuPDF,
libarchive, libmobi, additional fonts and dictionaries are **SD-card packages**,
not extra content in the internal SquashFS image.

Wi-Fi updates download both assets to the SD card and verify the GitHub asset
SHA-256. The package installer validates sizes, paths and every member hash,
checks free SD space, and extracts with bounded streaming buffers into a fresh
`.sonix/packages` generation. It activates only a fully verified generation.
Failure or cancellation preserves the active generation. Successful installs
retain the active and previous generations and prune older complete versions.
Manual/custom files in `Ebook/Fonts` and `Ebook/Dictionaries` are preserved and
take precedence over bundled files with the same identifier.

Older firmware updaters only download the `.upt`. After installing this build,
check the same Wi-Fi channel again to install its matching SD package without
reflashing firmware. Missing packages produce a readable error; TXT/EPUB do not
require the native-engine package.

On cards mounted `noexec`, the helper can run from a bounded RAM-backed `/tmp`
copy. It refuses persistent internal staging. Kindle import files remain on
the SD card under `.sonix/book-cache`; removing this cache removes imports,
not original books or saved reading positions.

The R1 packer retains the known kernel and drivers, checks MIPS/glibc ABI and
re-reads the final image and integrity chains. Its hard internal-storage guard
rejects any candidate SquashFS larger than the pinned working R1 base image.
That limit is not raised to accommodate packages.

## Limits and validation

The device has 64 MB RAM. Native parsing runs in separate processes with a
16 MiB MuPDF heap, address-space/CPU limits and a parent wall-clock deadline.
Inputs are limited to 128 MiB and 4096 native pages. Each response holds at most
256 KiB extracted text or a bounded 960×1280 page image. Very complex documents
may exceed these limits and fail without opening. Native document operations
can take several seconds on the R1. Plain-text section boundaries preserve
UTF-8 scalars and UTF-16 surrogate pairs; search phrases do not cross section
boundaries.

Automated checks cover generated document fixtures, native target ABI, TXT
encodings/Unicode boundaries, existing EPUB/search/bookmark regressions, and
package corruption/path/cancellation/rollback/pruning. Hardware validation on
the R1 remains required; test releases keep `hardware_tested:false`.

Pinned source references and redistribution licenses ship inside the native
package (`Ebook/Tools/native-reader-sources.json` and `licenses/`).
