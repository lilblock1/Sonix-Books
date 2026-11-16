#!/usr/bin/env python3
"""Read-only inventory comparing a verified R1 reference with a new userspace."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

def elf(path):
    if path.is_symlink() or not path.is_file():
        return None
    with path.open("rb") as f:
        if f.read(4) != b"\x7fELF":
            return None
    out = subprocess.run(["readelf", "-h", "-l", "-A", "-d", "-V", str(path)],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         text=True, check=True).stdout
    fields = {}
    for name in ("Class", "Data", "Machine", "Flags", "FP ABI"):
        m = re.search(r"^\s*" + re.escape(name) + r":\s*(.+)$", out, re.M)
        fields[name] = m.group(1).strip() if m else None
    interp = re.search(r"Requesting program interpreter: ([^\]]+)", out)
    fields["interpreter"] = interp.group(1) if interp else None
    fields["needed"] = sorted(set(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", out)))
    fields["glibc_versions_seen"] = sorted(set(re.findall(r"\bGLIBC_[0-9.]+\b", out)))
    return fields

def inventory(root):
    entries, abi, total = {}, {}, 0
    for p in sorted(root.rglob("*")):
        name = str(p.relative_to(root))
        if p.is_symlink():
            entries[name] = {"symlink": str(p.readlink())}
        elif p.is_file():
            size = p.stat().st_size
            total += size
            entries[name] = {"bytes": size}
            record = elf(p)
            if record:
                abi[name] = record
    return {"regular_file_bytes": total, "entries": entries, "elf": abi}

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""):
            h.update(block)
    return h.hexdigest()

reference, candidate, output, archive, squashfs = map(Path, sys.argv[1:])
old, new = inventory(reference), inventory(candidate)
for path, data in new["elf"].items():
    assert data["Class"] == "ELF32" and "little endian" in data["Data"] and "MIPS" in data["Machine"], (path,data)
assert new["elf"], "No target ELF binaries"
player = old["elf"].get("usr/bin/sonix_player")
assert player, "Missing reference player ELF"
busybox = new["elf"].get("bin/busybox")
assert busybox, "Missing candidate BusyBox"
report = {
    "flashable": False, "hardware_verified": False,
    "reference_player_abi": player, "candidate_busybox_abi": busybox,
    "fp_abi_matches": player["FP ABI"] is not None and player["FP ABI"] == busybox["FP ABI"],
    "reference_module_paths": [p for p in old["entries"] if p.endswith(".ko")],
    "reference_init_paths": [p for p in old["entries"] if p.startswith("etc/init.d/")],
    "reference_only_paths": sorted(set(old["entries"])-set(new["entries"])),
    "reference_rootfs": old, "candidate_rootfs": new,
    "candidate_tar_gz": {"bytes":archive.stat().st_size,"sha256":sha(archive)},
    "reference_squashfs": {"bytes":squashfs.stat().st_size,"sha256":sha(squashfs)},
    "notes": ["This is an inventory, not a compatibility proof.",
              "Compression formats differ; archive sizes do not establish flash compatibility.",
              "No physical hardware, device driver, UI or game testing was performed."]
}
output.write_text(json.dumps(report, indent=2)+"\n")
print("Reference / candidate FP ABI:", player["FP ABI"], "/", busybox["FP ABI"])
print("Candidate archive bytes:", archive.stat().st_size)
print("Non-flashable research artifact only")
