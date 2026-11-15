"""Compile the real EPUB search engine and verify generated, compressed books.

Default uses the production POSIX arena. --arena permits a local test-only
allocator adapter on other hosts; it does not change production source.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", nargs="+", default=["cc"])
    parser.add_argument("--arena", type=Path)
    parser.add_argument("--tempdir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="sonix-search-", dir=args.tempdir) as name:
        temp = Path(name).resolve()
        if args.tempdir:
            assert temp.is_relative_to(args.tempdir.resolve())
        book = temp / "search.epub"
        with zipfile.ZipFile(book, "w", compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
            z.writestr("META-INF/container.xml", '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            z.writestr("OPS/book.opf", '<package xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0"><metadata><dc:title>Search Fixture</dc:title><dc:creator>Test Author</dc:creator></metadata><manifest><item id="one" href="one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="two.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="one"/><itemref idref="two"/></spine></package>')
            z.writestr("OPS/one.xhtml", '<html><body><h1>Beginning</h1><p>A <em>styled</em> phrase &amp; needle.</p><p>猫と犬。 Another needle.</p></body></html>')
            z.writestr("OPS/two.xhtml", '<html><body><p>Later NEEDLE in another chapter.</p></body></html>')
        inflate = temp / "inflate.c"
        inflate.write_text('#include "src/system/image/miniz/miniz_tinfl.h"\n#include "src/system/image/miniz/tinfl_impl.inc"\n')
        sources = [root / "tests/test_ebooksearch_epub.c"]
        sources += [root / "src/system/ebook" / (s + ".c") for s in
                    ("ebook", "ebook_native", "ebook_plain", "ebookformats", "ebooksearch", "ebooktext", "epub_zip", "epub_opf", "epub_xhtml", "epub_css", "epub_xml")]
        sources += [root / "src/system/device/sdpackages_paths.c"]
        sources += [args.arena.resolve() if args.arena else root / "src/system/ebook/epub_arena.c", inflate]
        exe = temp / "search-test.exe"
        subprocess.run(args.cc + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(root)] +
                       [str(p) for p in sources] + ["-pthread", "-o", str(exe)], check=True, cwd=root)
        subprocess.run([str(exe), str(book)], check=True)


if __name__ == "__main__":
    main()
