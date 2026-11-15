"""Converter tests plus the real C reader against converter-produced files."""

import csv
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from dictionary_pack import build, normalize, verify, read_tsv, RECORD, HEADER
from build_dictionary_pack import wordnet_entries, PREFIX


class ConverterTests(unittest.TestCase):
    def test_matching_normalization(self):
        for raw, expected in [(" “CAFÉ,” ", "café"), ("DON’T", "don't"), ("ΟΣ", "οσ"),
                              ("ос", "ос"), ("КОТ", "кот"), ("north__ america", "north america"),
                              ("世界", "世界"), ("Straße", "straße")]:
            self.assertEqual(normalize(raw), expected.encode())
        for word in ["", "...", "bad\x01", "\ufffd", "q" * 96, "q" * 256, "界" * 32]:
            with self.assertRaises(ValueError):
                normalize(word)
        self.assertEqual(normalize("q" * 95), b"q" * 95)

    def test_merge_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as temp:
            first, second = Path(temp) / "one.sxd", Path(temp) / "two.sxd"
            values = [("Zed", "Last"), ("APPLE", "First"), ("apple", "Second"), ("apple", "First")]
            self.assertEqual(build(values, first), 2)
            build(values, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(verify(first), 2)
            data = first.read_bytes()
            word, start, length = RECORD.unpack_from(data, HEADER.size)
            self.assertEqual(word.rstrip(b"\0"), b"apple")
            self.assertEqual(data[start:start + length], b"First\n\nSecond")

    def test_bad_input_does_not_replace_output(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "dict.sxd"
            build([("good", "Kept")], output)
            original = output.read_bytes()
            for entries in [[], [("word", "")], [("word", "x\0y")], [("word", "x" * 65537)], [("q" * 96, "text")]]:
                with self.assertRaises(ValueError):
                    build(entries, output)
                self.assertEqual(output.read_bytes(), original)

    def test_tsv_multiline(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "input.tsv"
            with source.open("w", encoding="utf-8-sig", newline="") as file:
                csv.writer(file, delimiter="\t").writerow(["Café", "Line one\nLine two\twith a tab"])
            self.assertEqual(list(read_tsv(source)), [("Café", "Line one\nLine two\twith a tab")])

    def test_verifier_bounds_and_order(self):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "test.sxd"
            build([("a", "one"), ("b", "two")], file)
            original = file.read_bytes()
            for at, value in [(8, 0), (12, 103), (112, 0), (116, 65537)]:
                data = bytearray(original); struct.pack_into("<I", data, at, value); file.write_bytes(data)
                with self.assertRaises(ValueError):
                    verify(file)
            file.write_bytes(original + b"trailing")
            with self.assertRaises(ValueError):
                verify(file)

    def test_wordnet_parser_and_irregular_forms(self):
        with tempfile.TemporaryDirectory() as temp:
            filename = Path(temp) / "source.tar.bz2"
            with tarfile.open(filename, "w:bz2") as archive:
                for part in ("noun", "verb", "adj", "adv"):
                    data = ("  Copyright header\n" +
                            "00001740 03 n 02 entity 0 being 0 000 | A thing that exists.\n" if part == "noun" else
                            "00001740 03 v 01 go 0 000 | To move.\n" if part == "verb" else
                            "00001740 03 a 01 ready(p) 0 000 | Prepared.\n" if part == "adj" else "").encode()
                    for name, contents in [("data." + part, data), (part + ".exc", b"went go\n" if part == "verb" else b"")]:
                        info = tarfile.TarInfo(PREFIX + "dict/" + name); info.size = len(contents)
                        archive.addfile(info, io.BytesIO(contents))
            with tarfile.open(filename, "r:bz2") as archive:
                words = dict(wordnet_entries(archive))
            self.assertIn("entity", words); self.assertIn("being", words); self.assertIn("ready", words)
            self.assertIn("Inflected form of go", words["went"])


def integration():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        dictionary = root / "Ebook/Dictionaries/Fixture.sxd"
        entries = [("Apple", "A fruit.\nA second sense."), ("CAFÉ", "Coffee house."), ("don't", "Do not."),
                   ("north america", "A continent."), ("ΟΣ", "Greek folding."), ("КОТ", "Cyrillic folding."),
                   ("世界", "Unicode headword."), ("Straße", "No expanding case fold."),
                   ("q" * 95, "Maximum key."), ("long", "a" * 8191 + "π" + "b" * 20)]
        entries += [(f"entry{i:05}", f"Definition {i}") for i in range(10000)]
        build(entries, dictionary)
        binary = root / "test_dictionary"
        subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-I.",
                        "src/system/device/sdpackages_paths.c", "tests/test_ebookdict.c", "src/system/ebook/ebookdict.c", "src/system/ebook/ebooktext.c",
                        "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary), str(root)], check=True)


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False)
    if not result.result.wasSuccessful():
        raise SystemExit(1)
    if "--python-only" not in sys.argv:
        integration()
