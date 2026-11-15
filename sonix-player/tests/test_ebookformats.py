"""Verify real TXT decoding, section boundaries, escaping and memory bounds."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def compile_driver(output):
    inflate = output.parent / 'inflate.c'
    inflate.write_text('#include "src/system/image/miniz/miniz_tinfl.h"\n#include "src/system/image/miniz/tinfl_impl.inc"\n')
    sources = ['tests/ebook_formats_driver.c', 'src/system/device/sdpackages_paths.c']
    sources += ['src/system/ebook/' + name + '.c' for name in
                ('ebook', 'ebook_native', 'ebook_plain', 'ebookformats', 'ebooktext', 'epub_arena', 'epub_zip', 'epub_opf', 'epub_xhtml', 'epub_css', 'epub_xml')]
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-DHOST_BUILD=1', '-I.'] +
                   sources + [str(inflate), '-pthread', '-o', str(output)], cwd=ROOT, check=True)


def main():
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary); exe = folder / 'reader'; compile_driver(exe)
        sample = 'Native reader <sample> & literal text. Café 猫 😀'
        cases = [('utf8', sample.encode()), ('utf8bom', b'\xef\xbb\xbf' + sample.encode()),
                 ('utf16le', b'\xff\xfe' + sample.encode('utf-16le')), ('utf16be', b'\xfe\xff' + sample.encode('utf-16be')),
                 ('ansi', 'Café “sample” €'.encode('cp1252'))]
        for name, data in cases:
            book = folder / (name + '.txt'); book.write_bytes(data)
            expected = 'Café “sample” €' if name == 'ansi' else sample
            result = subprocess.check_output([str(exe), str(book)]).decode().strip()
            assert result == expected, (name, result)
        for encoding, prefix in [('utf-8', b''), ('utf-16le', b'\xff\xfe'), ('utf-16be', b'\xfe\xff')]:
            text = 'A' * (65534 if encoding == 'utf-8' else 32767) + '😀猫' + 'Z' * 10000
            book = folder / 'boundary.txt'; book.write_bytes(prefix + text.encode(encoding))
            actual = subprocess.check_output([str(exe), str(book)]).decode().replace('\n', '')
            assert actual == text, (encoding, len(actual), len(text))
        print('TXT UTF-8/UTF-16/Windows-1252, literal markup, Unicode section boundaries and memory cleanup passed')


if __name__ == '__main__':
    main()
