"""Build the separate SD-card font pack from pinned, checksummed upstream files."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import urllib.request
import zipfile


def copyright_names(data):
    for i in range(struct.unpack_from(">H", data, 4)[0]):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + 16 * i)
        if tag == b"name":
            break
    else:
        return []
    _, count, strings = struct.unpack_from(">HHH", data, offset)
    values = set()
    for i in range(count):
        platform, _, _, key, length, pos = struct.unpack_from(">6H", data, offset + 6 + 12 * i)
        if key == 0:
            raw = data[offset + strings + pos:offset + strings + pos + length]
            values.add(raw.decode("utf-16-be" if platform in (0, 3) else "mac_roman", errors="replace"))
    return sorted(values)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--staging", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    folder = root / "extras/reading-fonts"
    manifest = json.loads((folder / "font-manifest.json").read_text())
    payload = {}
    for file in manifest["files"]:
        name = PurePosixPath(file["path"])
        assert not name.is_absolute() and ".." not in name.parts
        if args.staging:
            data = args.staging.joinpath(*name.parts).read_bytes()
        else:
            assert file["source"].startswith("https://raw.githubusercontent.com/")
            with urllib.request.urlopen(file["source"], timeout=30) as response:
                data = response.read()
        assert len(data) == file["size"]
        assert hashlib.sha256(data).hexdigest() == file["sha256"], file["path"]
        payload[str(name)] = data
    for family in manifest["families"]:
        prefix = f"Ebook/Fonts/{family['name']}"
        notice = "Font copyright notices, reproduced from the unmodified Regular face:\n\n"
        notice += "\n\n".join(copyright_names(payload[f"{prefix}/Regular.ttf"]))
        notice += "\n\n" + (folder / "licenses" / f"{family['name']}.txt").read_text(encoding="utf-8")
        payload[f"{prefix}/LICENSE.txt"] = notice.encode("utf-8")
    payload["README.txt"] = (folder / "README.md").read_bytes()
    payload["font-manifest.json"] = (folder / "font-manifest.json").read_bytes()
    target = folder / "Sonix-Books-Fonts.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data, compresslevel=9)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix(".zip.sha256").write_text(f"{digest}  {target.name}\n")
    print(f"Built {len(manifest['families'])} families, {len(manifest['files'])} font files, {target.stat().st_size} bytes")


if __name__ == "__main__":
    main()
