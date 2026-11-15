"""Integration checks for the actual C installer, including failure atomicity."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bundle', ROOT / 'tools/build_sd_packages.py')
bundle = importlib.util.module_from_spec(spec); spec.loader.exec_module(bundle)


def main():
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary); card = folder / 'card'; card.mkdir()
        exe = folder / 'installer'
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I.', 'tests/sdpackages_driver.c',
                        'src/system/device/sdpackages.c', 'src/system/device/sdpackages_paths.c',
                        'src/system/core/json.c', 'src/system/core/sha256.c', '-o', str(exe)], cwd=ROOT, check=True)
        source = folder / 'components.zip'
        data = (b'font-test-data' * 10000) + bytes(range(256)) * 600
        with zipfile.ZipFile(source, 'w') as archive:
            archive.writestr('Ebook/Fonts/Test/Regular.ttf', data)
            archive.writestr('Ebook/Dictionaries/Test.sxd', b'dictionary')
            archive.writestr('Ebook/Tools/sonix_booktool', b'test-engine')
            archive.writestr('Ebook/Tools/mobitool', b'test-mobi')
        package = folder / bundle.ASSET
        def install(version, cancel=0, expected=None):
            digest = expected or hashlib.sha256(package.read_bytes()).hexdigest()
            result = subprocess.check_output([str(exe), str(card), str(package), version, digest, str(cancel)], text=True)
            status, active = result.strip().split(' ', 1)
            return int(status), active
        first = 'v0.3.0.1-test'
        bundle.build(first, [source], package, require_r1=False)
        assert install(first) == (0, first)
        base = card / '.sonix/packages'; previous_active = (base / 'active').read_bytes()
        active = base / previous_active.decode().strip()
        assert (active / 'Ebook/Fonts/Test/Regular.ttf').read_bytes() == data
        assert os.access(active / 'Ebook/Tools/sonix_booktool', os.X_OK)
        # Whole-asset digest failure, truncated payload, bad paths and member
        # hashes must never replace the previous generation.
        assert install(first, expected='0' * 64)[0] != 0
        valid = package.read_bytes()
        for mutated in (valid[:-10], valid[:-1] + bytes([valid[-1] ^ 1])):
            package.write_bytes(mutated); assert install(first)[0] != 0
            assert (base / 'active').read_bytes() == previous_active
        size = struct.unpack('<I', valid[8:12])[0]
        manifest = json.loads(valid[12:12 + size]); payload = valid[12 + size:]
        for bad in ('Ebook/Fonts/../../escape', 'Ebook/Tools/../escape', '/escape', 'Ebook/Fonts/a\\b', 'Ebook/Fonts/.hidden'):
            doc = dict(manifest); doc['files'] = [dict(item) for item in manifest['files']]; doc['files'][0]['path'] = bad
            encoded = json.dumps(doc).encode()
            package.write_bytes(b'SONIXPK1' + struct.pack('<I', len(encoded)) + encoded + payload)
            assert install(first)[0] != 0
            assert (base / 'active').read_bytes() == previous_active
        # A cancellation after extraction has started rolls back all staging.
        second = 'v0.3.0.2-test'; bundle.build(second, [source], package, require_r1=False)
        assert install(second, cancel=1)[0] != 0
        assert (base / 'active').read_bytes() == previous_active
        assert not list(base.glob('stage-*'))
        assert install(second) == (0, second)
        assert (base / 'previous').read_bytes() == previous_active
        third = 'v0.3.0.3-test'; bundle.build(third, [source], package, require_r1=False)
        assert install(third) == (0, third)
        generations = [p for p in base.iterdir() if p.is_dir()]
        assert len(generations) == 2 and not active.exists()
        # A legacy custom font remains outside generation management.
        custom = card / 'Ebook/Fonts/Custom.ttf'; custom.parent.mkdir(parents=True); custom.write_bytes(b'custom')
        assert install(third) == (0, third)
        assert custom.read_bytes() == b'custom'
        # OTA passes a pinned Linux directory descriptor via procfs. If
        # the original pathname is replaced, installation must still reach
        # the opened directory rather than the new filesystem/path.
        card_fd = os.open(card, os.O_RDONLY | os.O_DIRECTORY)
        try:
            detached = folder / 'original-card'
            card.rename(detached)
            card.mkdir()
            pinned = f'/proc/self/fd/{card_fd}/.'
            fourth = 'v0.3.0.4-test'
            bundle.build(fourth, [source], package, require_r1=False)
            digest = hashlib.sha256(package.read_bytes()).hexdigest()
            result = subprocess.check_output(
                [str(exe), pinned, str(package), fourth, digest, '0'],
                pass_fds=(card_fd,), text=True).strip()
            assert result == '0 ' + fourth, result
            assert not list(card.iterdir()), 'installer wrote through the replaced card path'
            managed = detached / '.sonix/packages'
            generation = (managed / 'active').read_text().strip()
            assert (managed / generation / '.complete').read_text().strip() == 'SDPKG1 ' + fourth
            assert (detached / 'Ebook/Fonts/Custom.ttf').read_bytes() == b'custom'
        finally:
            os.close(card_fd)
        print('SD package hashes, bounded streaming, path checks, cancellation, rollback, pruning and pinned-directory installation passed')


if __name__ == '__main__':
    main()
