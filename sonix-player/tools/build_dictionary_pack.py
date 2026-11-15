"""Produce the separate English SD-card pack from pinned WordNet 3.0 data."""

import argparse
import hashlib
import json
import re
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from dictionary_pack import build, normalize, verify

SOURCE = "https://wordnetcode.princeton.edu/3.0/WordNet-3.0.tar.bz2"
SHA256 = "6c492d0c7b4a40e7674d088191d3aa11f373bb1da60762e098b8ee2dda96ef22"
HASH_REFERENCE = "https://github.com/Homebrew/homebrew-core/blob/master/Formula/w/wordnet.rb"
PREFIX = "WordNet-3.0/"
LABELS = {"noun": "noun", "verb": "verb", "adj": "adjective", "adv": "adverb"}


def member_text(archive, name):
    member = archive.getmember(PREFIX + name)
    if not member.isfile() or member.size > 64 * 1024 * 1024:
        raise ValueError("invalid WordNet archive member")
    with archive.extractfile(member) as file:
        return file.read().decode("utf-8")


def wordnet_entries(archive):
    words, exceptions = {}, []
    for part, label in LABELS.items():
        for line in member_text(archive, "dict/data." + part).splitlines():
            if not line or not line[0].isdigit():
                continue
            fields, bar, gloss = line.partition("|")
            fields = fields.split()
            count = int(fields[3], 16)
            if not bar or not gloss.strip() or len(fields) < 4 + 2 * count:
                raise ValueError("invalid WordNet synset")
            for i in range(count):
                word = re.sub(r"\((?:a|p|ip)\)$", "", fields[4 + 2 * i]).replace("_", " ")
                key = normalize(word).decode("utf-8")
                value = label + ": " + gloss.strip()
                senses = words.setdefault(key, [])
                if value not in senses:
                    senses.append(value)
        exceptions.extend(line.split() for line in member_text(archive, "dict/" + part + ".exc").splitlines())
    # Keep real headwords; add explicit irregular forms such as "went" only
    # when WordNet does not already define the form itself.
    aliases = {}
    for row in exceptions:
        if len(row) < 2:
            continue
        word = normalize(row[0].replace("_", " ")).decode("utf-8")
        if word in words:
            continue
        for base in row[1:]:
            base = normalize(base.replace("_", " ")).decode("utf-8")
            if base in words:
                value = f"Inflected form of {base}.\n\n" + "\n\n".join(words[base])
                if value not in aliases.setdefault(word, []):
                    aliases[word].append(value)
    for word, senses in words.items():
        yield word, "\n\n".join(senses)
    for word, senses in aliases.items():
        yield word, "\n\n".join(senses)


def download(output):
    with urllib.request.urlopen(SOURCE, timeout=60) as response, open(output, "wb") as file:
        size = 0
        while chunk := response.read(1024 * 1024):
            size += len(chunk)
            if size > 32 * 1024 * 1024:
                raise ValueError("WordNet archive size limit")
            file.write(chunk)


def make_pack(source, output):
    digest = hashlib.sha256(Path(source).read_bytes()).hexdigest()
    if digest != SHA256:
        raise ValueError("WordNet archive checksum mismatch")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "Ebook/Dictionaries"
        root.mkdir(parents=True)
        with tarfile.open(source, "r:bz2") as archive:
            license_text = member_text(archive, "LICENSE")
            if "Copyright 2006 by Princeton University" not in license_text or "Permission to use, copy, modify" not in license_text:
                raise ValueError("WordNet license missing")
            count = build(wordnet_entries(archive), root / "English.sxd")
        assert verify(root / "English.sxd") == count
        (root / "WordNet-LICENSE.txt").write_text(license_text, encoding="utf-8")
        metadata = {
            "dictionary": "Princeton WordNet 3.0", "source": SOURCE,
            "source_sha256": SHA256, "checksum_reference": HASH_REFERENCE,
            "entries": count, "format": "SNXDICT1",
            "dictionary_sha256": hashlib.sha256((root / "English.sxd").read_bytes()).hexdigest(),
            "citation": "Princeton University. WordNet. https://wordnet.princeton.edu/",
            "changes": "Definitions grouped by headword and part of speech; explicit irregular aliases added; sorted disk index."
        }
        (root / "Dictionary-SOURCES.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        (root / "README.txt").write_text(
            "Sonix Books offline English dictionary\n\n"
            "Extract the Ebook folder to the SD-card root. Open a book, open its menu, and choose Dictionary.\n"
            "Dictionary data is outside the firmware; no Internet connection is used for lookups.\n\n"
            "Derived from Princeton WordNet 3.0. Copyright 2006 by Princeton University. All rights reserved.\n"
            "See WordNet-LICENSE.txt and Dictionary-SOURCES.json for license, attribution and modifications.\n",
            encoding="utf-8")
        package = output / "Sonix-Books-Dictionary.zip"
        with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zip_file:
            for filename in sorted(Path(temp).rglob("*")):
                if not filename.is_file():
                    continue
                info = zipfile.ZipInfo(filename.relative_to(temp).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                zip_file.writestr(info, filename.read_bytes())
        checksum = hashlib.sha256(package.read_bytes()).hexdigest()
        (output / (package.name + ".sha256")).write_text(checksum + "  " + package.name + "\n", encoding="ascii")
        print(json.dumps(metadata, indent=2))
        return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="use a local copy of the pinned archive")
    parser.add_argument("--output", type=Path, default=Path("dictionary-pack"))
    args = parser.parse_args()
    if args.archive:
        make_pack(args.archive, args.output)
    else:
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "WordNet-3.0.tar.bz2"
            download(archive)
            make_pack(archive, args.output)


if __name__ == "__main__":
    main()
