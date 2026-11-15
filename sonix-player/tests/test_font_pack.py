"""Verify every distributed font byte and license; optionally rasterize faces."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    folder = Path(__file__).resolve().parents[2] / "extras/reading-fonts"
    manifest = json.loads((folder / "font-manifest.json").read_text())
    data = (folder / "Sonix-Books-Fonts.zip").read_bytes()
    assert hashlib.sha256(data).hexdigest() == (folder / "Sonix-Books-Fonts.zip.sha256").read_text().split()[0]
    assert len(manifest["families"]) == 7 and len(manifest["files"]) == 25
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        assert archive.testzip() is None
        for family in manifest["families"]:
            license_text = archive.read(f"Ebook/Fonts/{family['name']}/LICENSE.txt").decode()
            assert "Copyright" in license_text or "copyright" in license_text
            assert "Permission" in license_text or "PERMISSION" in license_text or "permission" in license_text
        for font in manifest["files"]:
            raw = archive.read(font["path"])
            assert len(raw) == font["size"] and hashlib.sha256(raw).hexdigest() == font["sha256"]
            assert raw[:4] in (b"\0\1\0\0", b"OTTO", b"true")
            count = struct.unpack_from(">H", raw, 4)[0]
            assert 0 < count <= 4096 and 12 + 16 * count <= len(raw)
            for i in range(count):
                _, _, offset, length = struct.unpack_from(">4sIII", raw, 12 + i * 16)
                assert offset + length <= len(raw)
            if args.render:
                from PIL import ImageFont
                face = ImageFont.truetype(io.BytesIO(raw), 24)
                assert face.getmask("Reading preview I l 1 O 0 0123456789").getbbox()
    print("font pack: seven families, 25 faces, checksums, font containers and license notices passed" +
          ("; all faces rasterized with FreeType" if args.render else ""))


if __name__ == "__main__":
    main()
