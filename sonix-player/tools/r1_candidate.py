#!/usr/bin/env python3
"""Build an R1 candidate from a pinned Sonix R1 image; never flash a device."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import unicodedata
from datetime import datetime, timezone

BASE_SHA256 = "7617a76b212044ea0a3b533eb74cd3d04c28100cc3a08fd058834ef8ba6c4588"
BASE_URL = "https://github.com/Jepl4r/sonix-player/releases/download/1.3.0/r1.upt"
CHUNK = 524288
INFO = "usr/resource/sonix/components/system-info.json"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data, algorithm="sha256"):
    return hashlib.new(algorithm, data).hexdigest()


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True, stdout=subprocess.DEVNULL)


def manifest(path):
    records = []
    for line in path.read_text().splitlines():
        if "=" not in line:
            continue
        key, value = map(str.strip, line.split("=", 1))
        if key == "img_type":
            records.append({key: value})
        elif key.startswith("img_"):
            require(records and key not in records[-1], "Invalid image record")
            records[-1][key] = value
    require([(r.get("img_type"), r.get("img_name")) for r in records] ==
            [("kernel", "xImage"), ("rootfs", "rootfs.squashfs")],
            "Only the expected R1 kernel and application filesystem may be present")
    for record in records:
        require(set(record) == {"img_type", "img_name", "img_size", "img_md5"}, "Incomplete image record")
        require(re.fullmatch(r"[0-9a-f]{32}", record["img_md5"]), "Invalid image MD5")
        require(0 < int(record["img_size"]) <= 128 * 1024 * 1024, "Invalid image size")
    return records


def verify_images(ota):
    records = manifest(ota / "ota_update.in")
    images = {}
    for record in records:
        name, whole = record["img_name"], record["img_md5"]
        hashes = (ota / f"ota_md5_{name}.{whole}").read_text().splitlines()
        require(len(hashes) == (int(record["img_size"]) + CHUNK - 1) // CHUNK, "Wrong chunk count")
        require(all(re.fullmatch(r"[0-9a-f]{32}", h) for h in hashes), "Invalid chunk digest list")
        previous, blocks, paths = whole, [], set()
        for index, expected in enumerate(hashes):
            path = ota / f"{name}.{index:04d}.{previous}"
            block = path.read_bytes()
            require(0 < len(block) <= CHUNK, "Invalid chunk size")
            require(index == len(hashes) - 1 or len(block) == CHUNK, "Short non-final chunk")
            require(digest(block, "md5") == expected, "Corrupted firmware chunk")
            blocks.append(block)
            paths.add(path.name)
            previous = expected
        require({p.name for p in ota.glob(name + ".*")} == paths, "Unexpected or duplicate image chunks")
        data = b"".join(blocks)
        require(len(data) == int(record["img_size"]), "Image size mismatch")
        require(digest(data, "md5") == whole, "Whole-image digest mismatch")
        images[name] = data
    return records, images


def write_chunks(ota, name, data):
    whole = digest(data, "md5")
    previous, hashes = whole, []
    for index, offset in enumerate(range(0, len(data), CHUNK)):
        block = data[offset:offset + CHUNK]
        (ota / f"{name}.{index:04d}.{previous}").write_bytes(block)
        previous = digest(block, "md5")
        hashes.append(previous)
    (ota / f"ota_md5_{name}.{whole}").write_text("".join(h + "\n" for h in hashes))
    return whole


def snapshot(root):
    result = {}
    for folder, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            path = Path(folder) / name
            metadata = path.lstat()
            mode = metadata.st_mode
            if path.is_symlink():
                value = ("link", os.readlink(path), stat.S_IMODE(mode))
            elif path.is_file():
                value = ("file", digest(path.read_bytes()), stat.S_IMODE(mode))
            else:
                value = ("other", stat.S_IFMT(mode), stat.S_IMODE(mode), metadata.st_rdev)
            result[path.relative_to(root).as_posix()] = value + (metadata.st_uid, metadata.st_gid)
    return result


def check_elf(path):
    data = path.read_bytes()
    require(len(data) >= 52 and data[:6] == b"\x7fELF\x01\x01", "Player must be ELF32 little-endian")
    require(int.from_bytes(data[18:20], "little") == 8, "Player must target MIPS")
    require(int.from_bytes(data[16:18], "little") in (2, 3), "Player is not an executable")
    symbols = subprocess.check_output(["readelf", "--dyn-syms", "-W", str(path)], text=True)
    for major, minor in re.findall(r"@GLIBC_(\d+)\.(\d+)", symbols):
        require((int(major), int(minor)) <= (2, 22), "Player requires a newer glibc than the R1")


def build(base, player, repository, output):
    require(digest(base.read_bytes()) == BASE_SHA256, "Base is not the pinned Sonix 1.3.0 R1 image")
    check_elf(player)
    output.mkdir(parents=True, exist_ok=True)
    report = {"base_url": BASE_URL, "base_sha256": BASE_SHA256, "device": "HiBy R1",
              "flash_approved": False, "hardware_tested": False,
              "note": "Structural checks cannot guarantee safe flashing or device boot."}
    with tempfile.TemporaryDirectory(prefix="sonix-r1-build-") as temporary:
        work = Path(temporary)
        iso, root = work / "iso", work / "root"
        run("7z", "x", base, "-o" + str(iso), "-y")
        ota = iso / "ota_v0"
        records, images = verify_images(ota)
        original_rootfs = work / "base.squashfs"
        original_rootfs.write_bytes(images["rootfs.squashfs"])
        run("unsquashfs", "-no-progress", "-d", root, original_rootfs)
        info = json.loads((root / INFO).read_text())
        require(info.get("device-name") == "HiBy R1" and info.get("OS_version") == "1.3.0",
                "Base rootfs identity/version does not match")
        before = snapshot(root)
        binary = root / "usr/bin/sonix_player"
        shutil.copyfile(player, binary)
        binary.chmod(0o755)
        resources = repository / "sonix-packer/assets/R1/usr/resource/sonix"
        language_dir = root / "usr/resource/sonix/language"
        existing_languages = {unicodedata.normalize("NFC", p.name): p for p in language_dir.glob("*.ini")}
        language_changes = set()
        for language in (resources / "language").glob("*.ini"):
            # Firmware built on macOS may use decomposed Unicode filenames.
            # Keep their original spelling so saved language choices survive.
            target = existing_languages.get(unicodedata.normalize("NFC", language.name), language_dir / language.name)
            shutil.copyfile(language, target)
            language_changes.add(target.relative_to(root).as_posix())
        new_info = json.loads((resources / "components/system-info.json").read_text())
        require(new_info.get("device-name") == "HiBy R1", "Fork resources name another device")
        new_info["build_version"] = datetime.now(timezone.utc).strftime("%d%m%Y%H%M")
        (root / INFO).write_text(json.dumps(new_info, indent=4) + "\n")
        after = snapshot(root)
        # Optional reader engines and data belong to the SD bundle. Refuse
        # accidental packaging even if a future compression change makes it fit.
        sd_only = {"sonix_booktool", "mobitool", "Sonix-Books-SD-Packages.spk",
                   "Sonix-Books-Fonts.zip", "Sonix-Books-Dictionary.zip"}
        require(not any(Path(p).name in sd_only or p.lower().endswith(".sxd") for p in after),
                "An SD-only reader package was embedded in internal firmware")
        changed = sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))
        allowed = {"usr/bin/sonix_player", INFO} | language_changes
        require(set(changed) <= allowed, "An unexpected system file changed")
        # No kernel, module, boot script, system-library or flash-layout overlay.
        new_rootfs = work / "new.squashfs"
        # Same LZO format and block size as the base; spend more build CPU on
        # compression rather than increasing the accepted flash footprint.
        run("mksquashfs", root, new_rootfs, "-comp", "lzo", "-Xalgorithm", "lzo1x_999",
            "-Xcompression-level", "9", "-b", "131072", "-all-root", "-noappend", "-no-progress")
        data = new_rootfs.read_bytes()
        rechecked_root = work / "rechecked-root"
        run("unsquashfs", "-no-progress", "-d", rechecked_root, new_rootfs)
        require(snapshot(rechecked_root) == after, "Repacked filesystem content or permissions changed")
        report.update(version=new_info["OS_version"], changed_files=changed,
                      kernel_sha256=digest(images["xImage"]), kernel_content_unchanged=True,
                      base_rootfs_bytes=len(images["rootfs.squashfs"]), rootfs_bytes=len(data),
                      rootfs_limit_policy="not larger than the pinned base image",
                      sd_packages_embedded=False,
                      internal_headroom_bytes=len(images["rootfs.squashfs"]) - len(data))
        (output / "r1-build-report.json").write_text(json.dumps(report, indent=2) + "\n")
        require(len(data) <= len(images["rootfs.squashfs"]),
                f"Candidate rootfs {len(data)} exceeds known base {len(images['rootfs.squashfs'])}. Device partition proof is required before raising this limit.")
        for file in ota.glob("rootfs.squashfs.*"):
            file.unlink()
        for file in ota.glob("ota_md5_rootfs.squashfs.*"):
            file.unlink()
        new_md5 = write_chunks(ota, "rootfs.squashfs", data)
        records[1].update(img_size=str(len(data)), img_md5=new_md5)
        text = "ota_version=0\n\n" + "\n\n".join(
            "\n".join(f"{key}={record[key]}" for key in ("img_type", "img_name", "img_size", "img_md5"))
            for record in records) + "\n"
        (ota / "ota_update.in").write_text(text)
        verified_records, verified_images = verify_images(ota)
        require(verified_images["xImage"] == images["xImage"], "Kernel changed")
        require(verified_records[0] == records[0], "Kernel flash metadata changed")
        # Keep an unverified container out of the artifact directory even if
        # the final re-read fails.
        image = work / "r1.upt"
        run("genisoimage", "-o", image, "-J", "-r", iso)
        # Re-read the final container, not just the directory it came from.
        final = work / "final"
        run("7z", "x", image, "-o" + str(final), "-y")
        _, final_images = verify_images(final / "ota_v0")
        require(final_images == verified_images, "Final container verification failed")
        report.update(image_sha256=digest(image.read_bytes()), image_bytes=image.stat().st_size,
                      structural_checks_passed=True,
                      kernel_write_warning="The standard updater can rewrite the identical kernel; this is not an A/B update.")
        shutil.copyfile(image, output / "r1.upt")
        (output / "r1-build-report.json").write_text(json.dumps(report, indent=2) + "\n")
        (output / "SHA256SUMS").write_text(report["image_sha256"] + "  r1.upt\n")
        (output / "FLASH_NOT_APPROVED.txt").write_text(
            "Development candidate only. Match the installed Sonix version/board, prepare recovery, and test on hardware before release.\n"
            "The kernel bytes match Sonix R1 1.3.0, but the standard updater may rewrite that partition.\n")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.base.resolve(), args.player.resolve(), args.repository.resolve(), args.output.resolve())
