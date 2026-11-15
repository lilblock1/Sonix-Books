"""Publication gate policy tests. The tiny fixture is not a firmware image."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_test_release import verify, BASE_SHA256, KERNEL_SHA256, ROOTFS_BYTES

class TestRelease(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        image = b'publication-policy-fixture-only'
        sha = hashlib.sha256(image).hexdigest()
        (self.directory / 'r1.upt').write_bytes(image)
        (self.directory / 'SHA256SUMS').write_text(sha + '  r1.upt\n')
        self.report = {'version': '0.2.0.12', 'device': 'HiBy R1',
            'base_sha256': BASE_SHA256, 'kernel_sha256': KERNEL_SHA256,
            'structural_checks_passed': True, 'kernel_content_unchanged': True,
            'hardware_tested': False, 'flash_approved': False,
            'base_rootfs_bytes': ROOTFS_BYTES, 'rootfs_bytes': ROOTFS_BYTES,
            'image_bytes': len(image), 'image_sha256': sha}
    def check(self, report=None, build='12'):
        (self.directory / 'r1-build-report.json').write_text(json.dumps(report or self.report))
        return verify(self.directory, '0.2.0', build)
    def test_good(self):
        result = self.check()
        self.assertEqual(result['tag'], 'v0.2.0.12-test')
        self.assertFalse(result['hardware_tested'])
        self.assertTrue(result['prerelease'])
    def test_reject_audit_changes(self):
        for key, bad in [('device','HiBy R3 Pro II'), ('base_sha256','bad'),
                ('kernel_sha256','bad'), ('kernel_content_unchanged',False),
                ('structural_checks_passed',False), ('structural_checks_passed',1),
                ('rootfs_bytes',ROOTFS_BYTES+1), ('base_rootfs_bytes',ROOTFS_BYTES+1),
                ('version','0.2.0.11'), ('hardware_tested',True), ('flash_approved',True),
                ('image_bytes',999), ('image_sha256','bad')]:
            report = copy.deepcopy(self.report)
            report[key] = bad
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(report)
    def test_reject_image_changes(self):
        (self.directory / 'r1.upt').write_bytes(b'changed')
        with self.assertRaises(ValueError): self.check()
    def test_reject_manifest_changes(self):
        (self.directory / 'SHA256SUMS').write_text('bad')
        with self.assertRaises(ValueError): self.check()
    def test_reject_build_identifiers(self):
        for bad in ['0','-1','12-test','1/2','1234567890']:
            with self.subTest(build=bad), self.assertRaises(ValueError): self.check(build=bad)

if __name__ == '__main__': unittest.main()
