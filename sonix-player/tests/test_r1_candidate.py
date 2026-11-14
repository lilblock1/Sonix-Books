import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("candidate", Path(__file__).resolve().parents[1] / "tools/r1_candidate.py")
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


class ImageTests(unittest.TestCase):
    def fixture(self, folder):
        records = []
        for kind, name, data in (("kernel", "xImage", b"test kernel"),
                                 ("rootfs", "rootfs.squashfs", b"R" * (candidate.CHUNK + 15))):
            checksum = candidate.write_chunks(folder, name, data)
            records.append(f"img_type={kind}\nimg_name={name}\nimg_size={len(data)}\nimg_md5={checksum}")
        (folder / "ota_update.in").write_text("ota_version=0\n\n" + "\n\n".join(records) + "\n")

    def test_round_trip_and_corrupt_chunk(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.fixture(folder)
            records, images = candidate.verify_images(folder)
            self.assertEqual(len(records), 2)
            self.assertEqual(images["xImage"], b"test kernel")
            chunk = next(folder.glob("rootfs.squashfs.0001.*"))
            chunk.write_bytes(b"corrupt")
            with self.assertRaises(ValueError):
                candidate.verify_images(folder)

    def test_extra_image_and_duplicate_chunk_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.fixture(folder)
            extra = folder / ("xImage.9999." + "0" * 32)
            extra.write_bytes(b"unexpected")
            with self.assertRaises(ValueError):
                candidate.verify_images(folder)
            extra.unlink()
            with (folder / "ota_update.in").open("a") as out:
                out.write("\nimg_type=bootloader\nimg_name=u-boot\nimg_size=1\nimg_md5=" + "0" * 32 + "\n")
            with self.assertRaises(ValueError):
                candidate.verify_images(folder)

    def test_host_elf_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            host = Path(temporary) / "host"
            host.write_bytes(b"\x7fELF\x02\x01" + bytes(80))
            with self.assertRaises(ValueError):
                candidate.check_elf(host)


if __name__ == "__main__":
    unittest.main()
