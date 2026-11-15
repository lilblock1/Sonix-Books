"""Build/verify the portable Sonix on-disk dictionary; Python stdlib only."""

import argparse
import csv
import os
import struct
import tempfile
from pathlib import Path

MAGIC = b"SNXDICT1"
WORD_BYTES = 96
RECORD = struct.Struct("<96sII")
HEADER = struct.Struct("<8sII")
MAX_ENTRIES = 500_000
MAX_FILE = 128 * 1024 * 1024
MAX_DEFINITION = 65_536
EDGE = set(".,;:!?()[]{}\"'\u2018\u2019\u201c\u201d")
SPACE = set(" \t\r\n\u00a0_")


def fold(c):
    n = ord(c)
    if (65 <= n <= 90 or 0xC0 <= n <= 0xDE and n != 0xD7 or
            0x391 <= n <= 0x3AB and n != 0x3A2 or 0x410 <= n <= 0x42F):
        n += 32
    elif 0x400 <= n <= 0x40F:
        n += 80
    elif ((0x100 <= n <= 0x12F or 0x132 <= n <= 0x137 or 0x14A <= n <= 0x177) and not n & 1):
        n += 1
    elif n == 0x178:
        n = 0xFF
    elif n == 0x3C2:
        n = 0x3C3
    return chr(n)


def normalize(word):
    if len(word.encode("utf-8")) >= 256:
        raise ValueError("lookup text exceeds 255 UTF-8 bytes")
    if any(c == "\ufffd" or ord(c) == 127 or ord(c) < 32 and c not in SPACE for c in word):
        raise ValueError("invalid headword")
    first, last = 0, len(word)
    while first < last and word[first] in EDGE | SPACE:
        first += 1
    while last > first and word[last - 1] in EDGE | SPACE:
        last -= 1
    value, gap = [], False
    for c in word[first:last]:
        if c in SPACE:
            gap = bool(value)
            continue
        if gap:
            value.append(" ")
        gap = False
        c = {"\u2019": "'", "\u2010": "-", "\u2011": "-"}.get(c, c)
        value.append(fold(c))
    key = "".join(value).encode("utf-8")
    if not key or len(key) >= WORD_BYTES:
        raise ValueError("empty or overlong normalized headword")
    return key


def definition(text):
    encoded = text.encode("utf-8")
    if not encoded or len(encoded) > MAX_DEFINITION or any(
            c == "\0" or ord(c) == 127 or ord(c) < 32 and c not in "\t\r\n" for c in text):
        raise ValueError("empty, overlong or invalid definition")
    return encoded


def build(entries, output):
    """Merge equal normalized words, then write a deterministic, sorted index."""
    words = {}
    for word, text in entries:
        key = normalize(word)
        definition(text)
        senses = words.setdefault(key, [])
        if text not in senses:
            senses.append(text)
    if not words or len(words) > MAX_ENTRIES:
        raise ValueError("dictionary entry limit")
    records = [(key, definition("\n\n".join(senses))) for key, senses in sorted(words.items())]
    offset = HEADER.size + len(records) * RECORD.size
    size = offset + sum(len(text) for _, text in records)
    if size > MAX_FILE:
        raise ValueError("dictionary exceeds 128 MiB")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    handle, temp = tempfile.mkstemp(prefix=output.name + ".", suffix=".tmp", dir=output.parent)
    try:
        with os.fdopen(handle, "wb") as file:
            file.write(HEADER.pack(MAGIC, len(records), RECORD.size))
            for word, text in records:
                file.write(RECORD.pack(word, offset, len(text)))
                offset += len(text)
            for _, text in records:
                file.write(text)
        verify(temp)
        os.replace(temp, output)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return len(records)


def verify(filename):
    size = Path(filename).stat().st_size
    if not HEADER.size <= size <= MAX_FILE:
        raise ValueError("dictionary file size")
    with open(filename, "rb") as file:
        magic, count, record_size = HEADER.unpack(file.read(HEADER.size))
        if magic != MAGIC or record_size != RECORD.size or not 0 < count <= MAX_ENTRIES:
            raise ValueError("dictionary header")
        end = HEADER.size + count * RECORD.size
        if end > size:
            raise ValueError("truncated index")
        previous, position = b"", end
        for i in range(count):
            file.seek(HEADER.size + i * RECORD.size)
            word, offset, length = RECORD.unpack(file.read(RECORD.size))
            key, separator, padding = word.partition(b"\0")
            if not separator or any(padding) or key <= previous or normalize(key.decode("utf-8")) != key:
                raise ValueError("invalid index order/headword")
            if offset != position or not 0 < length <= MAX_DEFINITION or offset + length > size:
                raise ValueError("definition bounds")
            file.seek(offset)
            definition(file.read(length).decode("utf-8"))
            previous, position = key, offset + length
        if position != size:
            raise ValueError("trailing dictionary bytes")
    return count


def read_tsv(filename):
    # CSV quoting permits tabs/newlines inside a definition on Windows too.
    with open(filename, encoding="utf-8-sig", newline="") as file:
        for number, row in enumerate(csv.reader(file, delimiter="\t"), 1):
            if not row:
                continue
            if len(row) != 2:
                raise ValueError(f"TSV row {number}: expected headword and definition")
            yield row[0], row[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="UTF-8 TSV: headword TAB definition")
    parser.add_argument("output", type=Path, help="output .sxd file")
    args = parser.parse_args()
    count = build(read_tsv(args.input), args.output)
    print(f"Built and verified {count:,} entries: {args.output}")


if __name__ == "__main__":
    main()
