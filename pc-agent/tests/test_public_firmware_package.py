"""Offline release checks: no serial connection, BLE or media actions."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('luna_public_merge', ROOT / 'scripts/merge-public-firmware.py')
merge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(merge)


class PublicFirmwarePackageTests(unittest.TestCase):
    def test_actual_release_payload_and_nvs_padding(self):
        directory = merge.RELEASE
        manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
        images = merge.source_images(directory, manifest)
        data = (directory / merge.OUTPUT).read_bytes()
        merge.verify_merged(data, images)
        self.assertEqual(data[0x11000:0x18000], b'\xff' * 0x7000)
        self.assertLess((len(data) + 0xFFF) & ~0xFFF, 0xA20000)
        merge.merge(check=True)

    def test_reject_corrupt_input_before_running_esptool(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ['manifest.json'] + [name for name, _ in merge.INPUTS]:
                shutil.copyfile(merge.RELEASE / name, directory / name)
            app = directory / 'luna-panel.bin'
            with app.open('r+b') as stream:
                stream.write(b'INVALID')
            with patch.object(merge.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                    merge.merge(directory)
                run.assert_not_called()
            self.assertFalse((directory / merge.OUTPUT).exists())

    def test_detect_shifted_component_and_added_padding(self):
        manifest = json.loads((merge.RELEASE / 'manifest.json').read_text(encoding='utf-8'))
        images = merge.source_images(merge.RELEASE, manifest)
        data = bytearray((merge.RELEASE / merge.OUTPUT).read_bytes())
        data[0x2000] ^= 1
        with self.assertRaisesRegex(ValueError, 'component differs'):
            merge.verify_merged(data, images)
        with self.assertRaisesRegex(ValueError, 'size/base offset'):
            merge.verify_merged((merge.RELEASE / merge.OUTPUT).read_bytes() + b'\xff', images)


if __name__ == '__main__':
    unittest.main()
