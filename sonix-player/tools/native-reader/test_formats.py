"""Exercise the real isolated engine with small, locally generated documents."""
import argparse
import binascii
import io
import re
import struct
import subprocess
import tarfile
import tempfile
from pathlib import Path
import zipfile
import zlib


def png():
    def block(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + block(b'IHDR', struct.pack('>IIBBBBB', 20, 30, 8, 2, 0, 0, 0)) + block(b'IDAT', zlib.compress((b'\0' + b'\xff\xff\xff' * 20) * 30)) + block(b'IEND', b'')


def fixtures(folder):
    (folder / 'book.html').write_text('<html><head><title>Reader test</title></head><body><h1>Sonix</h1><p>Native reader sample.</p></body></html>')
    (folder / 'book.md').write_text('# Sonix\n\nNative reader **sample**.\n')
    (folder / 'book.rtf').write_text(r'{\rtf1\ansi\ansicpg1252 Sonix\par Native reader sample.}')
    (folder / 'book.fb2').write_text('<?xml version="1.0" encoding="UTF-8"?><FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0"><description><title-info><genre>other</genre><book-title>Sonix</book-title><lang>en</lang></title-info></description><body><section><p>Native reader sample.</p></section></body></FictionBook>')
    with zipfile.ZipFile(folder / 'book.fbz', 'w') as archive:
        archive.write(folder / 'book.fb2', 'book.fb2')
    with zipfile.ZipFile(folder / 'book.docx', 'w') as archive:
        archive.writestr('[Content_Types].xml', '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>')
        archive.writestr('_rels/.rels', '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>')
        archive.writestr('word/_rels/document.xml.rels', '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>')
        archive.writestr('word/document.xml', '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>Native reader sample.</w:t></w:r></w:p></w:body></w:document>')
    with zipfile.ZipFile(folder / 'book.odt', 'w') as archive:
        archive.writestr('mimetype', 'application/vnd.oasis.opendocument.text')
        archive.writestr('content.xml', '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"><office:body><office:text><text:p>Native reader sample.</text:p></office:text></office:body></office:document-content>')
    with zipfile.ZipFile(folder / 'book.cbz', 'w') as archive:
        archive.writestr('001.png', png())
        archive.writestr('002.png', png())
    with tarfile.open(folder / 'book.cbt', 'w') as archive:
        entry = tarfile.TarInfo('001.png'); data = png(); entry.size = len(data)
        archive.addfile(entry, io.BytesIO(data))
    def rar_header(kind, flags, body):
        header = struct.pack('<BHH', kind, flags, 7 + len(body)) + body
        return struct.pack('<H', binascii.crc32(header) & 0xffff) + header
    name, data = b'001.png', png()
    rar = b'Rar!\x1a\x07\x00' + rar_header(0x73, 0, b'\0' * 6)
    fields = struct.pack('<IIBIIBBHI', len(data), len(data), 3, binascii.crc32(data) & 0xffffffff, 0, 20, 0x30, len(name), 0o100644)
    rar += rar_header(0x74, 0x8000, fields + name) + data + rar_header(0x7b, 0, b'')
    (folder / 'book.cbr').write_bytes(rar)
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
               b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    text = b'BT /F1 16 Tf 20 350 Td (Native reader sample.) Tj ET'
    objects.append(b'<< /Length ' + str(len(text)).encode() + b' >>\nstream\n' + text + b'\nendstream')
    data = bytearray(b'%PDF-1.4\n'); offsets = [0]
    for i, obj in enumerate(objects, 1):
        offsets.append(len(data)); data.extend(f'{i} 0 obj\n'.encode() + obj + b'\nendobj\n')
    xref = len(data); data.extend(f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode())
    for offset in offsets[1:]: data.extend(f'{offset:010d} 00000 n \n'.encode())
    data.extend(f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode())
    (folder / 'book.pdf').write_bytes(data)


def call(tool, operation, filename, root, index=0, success=True):
    result = subprocess.run([str(tool), operation, str(index), str(filename), str(root)], capture_output=True, timeout=50, check=True)
    assert len(result.stdout) >= 408, (filename.name, result.stderr)
    magic, status, pages, flags, text_size, png_size = struct.unpack('<4s5I', result.stdout[:24])
    assert magic == b'BK01' and len(result.stdout) == 408 + text_size + png_size
    assert (status == 0) == success, (filename.name, operation, status, result.stderr)
    return pages, flags, result.stdout[408:408 + text_size], result.stdout[408 + text_size:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tool', type=Path)
    parser.add_argument('--kindle', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary); fixtures(folder)
        for extension in ('html', 'md', 'rtf', 'fb2', 'fbz', 'docx', 'odt', 'pdf', 'cbz', 'cbr', 'cbt'):
            book = folder / ('book.' + extension)
            pages, flags, _, _ = call(args.tool, 'info', book, folder)
            assert pages > 0
            if extension in ('pdf', 'cbz', 'cbr', 'cbt'):
                _, _, _, image = call(args.tool, 'page', book, folder)
                assert image.startswith(b'\x89PNG')
            if extension not in ('cbz', 'cbr', 'cbt'):
                _, _, text, _ = call(args.tool, 'text', book, folder)
                assert b'Native reader sample.' in re.sub(rb'<[^>]+>', b'', text), (extension, text)
            print(extension, 'passed', flush=True)
        (folder / 'bad.pdf').write_bytes(b'broken')
        call(args.tool, 'page', folder / 'bad.pdf', folder, success=False)
        call(args.tool, 'page', folder / 'book.pdf', folder, index=999999, success=False)
        if args.kindle:
            for extension in ('mobi', 'azw3'):
                book = folder / ('book.' + extension)
                subprocess.run(['ebook-convert', str(folder / 'book.html'), str(book)], check=True, timeout=90, stdout=subprocess.DEVNULL)
                _, flags, prepared, _ = call(args.tool, 'prepare', book, folder)
                assert flags & 2 and Path(prepared.decode()).is_file()
                with zipfile.ZipFile(prepared.decode()) as archive:
                    assert any(name.endswith('.opf') for name in archive.namelist())
                assert call(args.tool, 'prepare', book, folder)[2] == prepared
                print(extension, 'passed', flush=True)


if __name__ == '__main__':
    main()
