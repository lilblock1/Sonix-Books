"""Build the streaming SD package. Large components never enter the firmware."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import tempfile
import zipfile
import zlib

MAX_BYTES = 256 * 1024 * 1024
MAX_FILE = 128 * 1024 * 1024
MAX_FILES = 1024
ASSET = "Sonix-Books-SD-Packages.spk"


def safe_path(name):
    parts = name.split("/")
    return (len(name.encode("utf-8")) < 256 and len(parts) <= 16
            and name.startswith(("Ebook/Fonts/", "Ebook/Dictionaries/", "Ebook/Tools/"))
            and all(part and not part.startswith(".") for part in parts)
            and not any(ord(c) < 32 or ord(c) > 126 or c in "\\:" for c in name))


def build(version, archives, output, require_r1=True):
    if not re.fullmatch(r"(?:v|[0-9])[A-Za-z0-9.-]{0,39}", version) or ".." in version:
        raise ValueError("invalid release version")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    entries, names, total = [], set(), 0
    native_ok = False
    with tempfile.TemporaryFile() as payload:
        for filename in archives:
            with zipfile.ZipFile(filename) as archive:
                for info in sorted(archive.infolist(), key=lambda item: item.filename):
                    if info.is_dir():
                        continue
                    name = info.filename
                    if not name.startswith("Ebook/"):
                        continue  # artifact wrappers may carry reports beside the pack
                    if not safe_path(name) or name.casefold() in names or info.file_size > MAX_FILE:
                        raise ValueError("invalid or duplicate package member: " + name)
                    mode = info.external_attr >> 16
                    if mode & 0o170000 == 0o120000:
                        raise ValueError("symlinks are not package members")
                    if name == "Ebook/Tools/native-reader-sources.json":
                        native_ok = json.loads(archive.read(info)).get("target") == "r1"
                    digest = hashlib.sha256()
                    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
                    before, size = payload.tell(), 0
                    with archive.open(info) as source:
                        while chunk := source.read(65536):
                            size += len(chunk)
                            total += len(chunk)
                            if size > MAX_FILE or total > MAX_BYTES:
                                raise ValueError("SD package size limit")
                            digest.update(chunk)
                            payload.write(compressor.compress(chunk))
                    payload.write(compressor.flush())
                    if size != info.file_size:
                        raise ValueError("short package member")
                    names.add(name.casefold())
                    entries.append(dict(path=name, bytes=size, compressed=payload.tell() - before, sha256=digest.hexdigest()))
                    if len(entries) > MAX_FILES:
                        raise ValueError("too many SD package files")
        if require_r1 and (not native_ok or not {"ebook/tools/sonix_booktool", "ebook/tools/mobitool"} <= names):
            raise ValueError("the bundle must contain the verified R1 native reader pack")
        if not entries:
            raise ValueError("empty SD package")
        manifest = json.dumps(dict(schema=1, version=version, target="r1", unpacked_bytes=total, files=entries),
                              ensure_ascii=True, separators=(",", ":")).encode("utf-8")
        if len(manifest) > 512 * 1024 or payload.tell() + len(manifest) + 12 > MAX_BYTES:
            raise ValueError("SD package size limit")
        temporary = output.with_suffix(output.suffix + ".part")
        try:
            with temporary.open("wb") as destination:
                destination.write(b"SONIXPK1" + struct.pack("<I", len(manifest)) + manifest)
                payload.seek(0)
                while chunk := payload.read(65536):
                    destination.write(chunk)
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
    with output.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    output.with_name(output.name + ".sha256").write_text(digest + "  " + output.name + "\n", encoding="ascii")
    return dict(version=version, files=len(entries), unpacked_bytes=total, bytes=output.stat().st_size, sha256=digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path(ASSET))
    parser.add_argument("archives", type=Path, nargs="+")
    args = parser.parse_args()
    print(json.dumps(build(args.version, args.archives, args.output), indent=2))


if __name__ == "__main__":
    main()
