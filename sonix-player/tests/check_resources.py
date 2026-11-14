"""Check the deployable resource sets as well as the C fallback repository."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
required = {"books_continue", "books_games", "books_card_unavailable", "books_reader_controls"}
languages = None
for model, device in (("R1", "HiBy R1"), ("R3PII", "HiBy R3 Pro II")):
    resources = root / "sonix-packer" / "assets" / model / "usr/resource/sonix"
    info = json.loads((resources / "components/system-info.json").read_text())
    assert info["device-name"] == device, (model, "model identity changed")
    assert info["ota-repo"] == "lilblock1/Sonix-Books", (model, "wrong updater destination")
    files = sorted((resources / "language").glob("*.ini"))
    assert len(files) == 12
    names = {file.name for file in files}
    assert languages is None or names == languages
    languages = names
    for file in files:
        entries = {}
        for line in file.read_text(encoding="utf-8-sig").splitlines():
            if "=" not in line or line.lstrip().startswith(("#", ";")):
                continue
            key, value = map(str.strip, line.split("=", 1))
            if key in required:
                assert key not in entries, (file, "duplicate", key)
                entries[key] = value
        assert required == entries.keys() and all(entries.values()), file
source = (root / "sonix-player/src/system/device/ota.c").read_text()
assert '#define DEFAULT_REPO "lilblock1/Sonix-Books"' in source
print("resources: both models, all 24 language files and updater destination passed")
