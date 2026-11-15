"""Gate opt-in prerelease publication; this does not attest hardware testing."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from r1_candidate import BASE_SHA256

ROOTFS_BYTES = 44_638_208
KERNEL_SHA256 = '9222276c59d7bc9ffb8594106475b986bf66066c4f8f4db99c6fb34cb4fc64f6'

def verify(directory, base_version, build):
    if not re.fullmatch(r'\d+\.\d+\.\d+', base_version):
        raise ValueError('Expected three-part source version')
    if not re.fullmatch(r'[1-9]\d{0,8}', build):
        raise ValueError('Invalid build number')
    report = json.loads((directory / 'r1-build-report.json').read_text())
    version = base_version + '.' + build
    required = {
        'version': version, 'device': 'HiBy R1',
        'base_sha256': BASE_SHA256, 'kernel_sha256': KERNEL_SHA256,
        'structural_checks_passed': True, 'kernel_content_unchanged': True,
        'hardware_tested': False, 'flash_approved': False,
        'base_rootfs_bytes': ROOTFS_BYTES,
    }
    for key, value in required.items():
        if type(report.get(key)) is not type(value) or report[key] != value:
            raise ValueError('Invalid candidate field: ' + key)
    size = report.get('rootfs_bytes')
    if type(size) is not int or not 0 < size <= ROOTFS_BYTES:
        raise ValueError('Root filesystem limit exceeded')
    image = directory / 'r1.upt'
    image_size = image.stat().st_size
    if not 0 < image_size <= 128 * 1024 * 1024 or report.get('image_bytes') != image_size:
        raise ValueError('Invalid image size')
    sha = hashlib.sha256(image.read_bytes()).hexdigest()
    if sha != report.get('image_sha256'):
        raise ValueError('Image does not match its audit')
    if (directory / 'SHA256SUMS').read_text().strip() != sha + '  r1.upt':
        raise ValueError('Checksum manifest mismatch')
    return {'tag': 'v' + version + '-test', 'version': version,
            'image_sha256': sha, 'hardware_tested': False, 'prerelease': True}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--build', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2] / 'sonix-packer/assets/R1/usr/resource/sonix/components/system-info.json'
    result = verify(args.candidate, json.loads(source.read_text())['OS_version'], args.build)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('Verified experimental release: ' + result['tag'])
