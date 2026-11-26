"""Reproduce/check the public P4 first-install image without accessing a device."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
import tempfile

RELEASE = Path(__file__).resolve().parents[1] / 'firmware/releases/2026.10.08-preview'
OUTPUT = 'luna-first-install-0x0.bin'
INPUTS = [('bootloader.bin', 0x2000), ('partition-table.bin', 0x10000),
          ('luna-panel.bin', 0x20000)]


def source_images(directory, manifest):
    if manifest['chip'] != 'esp32p4' or manifest['flash_settings'] != {
            'flash_mode': 'dio', 'flash_size': '32MB', 'flash_freq': '80m'}:
        raise ValueError('Only the published ESP32-P4 32MB/DIO/80m package is supported')
    entries = {entry['file']: entry for entry in manifest['files']}
    images = []
    for name, offset in INPUTS:
        entry = entries[name]
        data = (directory / name).read_bytes()
        if (int(entry['offset'], 0) != offset or len(data) != entry['size_bytes'] or
                hashlib.sha256(data).hexdigest() != entry['sha256']):
            raise ValueError(f'Input offset/size/SHA-256 mismatch: {name}')
        images.append((name, offset, data))
    table = images[1][2]
    partitions = {}
    for index in range(0, len(table), 32):
        record = table[index:index + 32]
        if record[:2] != b'\xaa\x50':
            break
        name = record[12:28].split(b'\0', 1)[0].decode('ascii')
        partitions[name] = (int.from_bytes(record[4:8], 'little'),
                            int.from_bytes(record[8:12], 'little'))
    if (partitions.get('nvs') != (0x11000, 0x6000) or
            partitions.get('phy_init') != (0x17000, 0x1000) or
            partitions.get('factory') != (0x20000, 0xA00000) or
            partitions.get('storage') != (0xA20000, 0x500000)):
        raise ValueError('Unexpected B1 partition layout; re-audit before merging')
    if len(images[0][2]) > 0xE000 or len(images[2][2]) > 0xA00000:
        raise ValueError('Image exceeds its allocated region')
    return images


def verify_merged(data, images):
    expected_end = images[-1][1] + len(images[-1][2])
    if len(data) != expected_end:
        raise ValueError('Merged size/base offset mismatch; do not pad to full flash size')
    cursor = 0
    for name, offset, original in images:
        if data[cursor:offset] != b'\xff' * (offset - cursor):
            raise ValueError(f'Unexpected padding before {name}')
        if data[offset:offset + len(original)] != original:
            raise ValueError(f'Merged component differs from published input: {name}')
        cursor = offset + len(original)


def merge(directory=RELEASE, check=False):
    directory = Path(directory)
    manifest_path = directory / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    images = source_images(directory, manifest)
    output = directory / OUTPUT
    if check:
        data = output.read_bytes()
    else:
        if importlib.metadata.version('esptool') != '5.4.0':
            raise ValueError('Use esptool==5.4.0 to reproduce this package')
        with tempfile.TemporaryDirectory(prefix='luna-merge-') as temporary:
            target = Path(temporary) / OUTPUT
            command = [sys.executable, '-m', 'esptool', '--chip', 'esp32p4',
                       'merge-bin', '--format', 'raw', '--target-offset', '0x0',
                       '--flash-mode', 'keep', '--flash-freq', 'keep', '--flash-size', 'keep',
                       '--output', str(target)]
            for name, offset, _ in images:
                command.extend([hex(offset), str(directory / name)])
            subprocess.run(command, check=True)
            data = target.read_bytes()
        verify_merged(data, images)
        output.write_bytes(data)
    verify_merged(data, images)
    entry = dict(file=OUTPUT, offset='0x0', size_bytes=len(data),
                 sha256=hashlib.sha256(data).hexdigest(), install_mode='first_install_only')
    if check:
        recorded = next(item for item in manifest['files'] if item['file'] == OUTPUT)
        if entry != recorded:
            raise ValueError('Merged image metadata differs from manifest')
        if f"{entry['sha256']}  {OUTPUT}" not in (directory / 'SHA256SUMS.txt').read_text(encoding='utf-8'):
            raise ValueError('Merged image checksum entry is missing')
    else:
        manifest['files'] = [item for item in manifest['files'] if item['file'] != OUTPUT] + [entry]
        manifest['merged_image'] = dict(
            format='raw', tool='esptool 5.4.0', target_offset='0x0', gap_fill='0xFF',
            inputs=[dict(file=name, offset=hex(offset)) for name, offset, _ in images],
            write_end_exclusive=hex(len(data)),
            erase_end_exclusive=hex((len(data) + 0xFFF) & ~0xFFF),
            overwrites=['bootloader', 'partition table', 'NVS including BLE bonds/weather cache',
                        'phy_init', 'factory app'],
            storage_offset='0xA20000', full_flash_padding=False,
            warning='First installation only. RAW padding overwrites NVS; use app-only for existing devices.')
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        (directory / 'SHA256SUMS.txt').write_text(''.join(
            f"{item['sha256']}  {item['file']}\n" for item in manifest['files']), encoding='utf-8')
    print(f"{OUTPUT}: {len(data)} bytes, SHA256 {entry['sha256']}")
    print('Inputs byte-identical; offset 0x0; FF gaps include NVS. First installation only; no device accessed.')
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify existing output without esptool or writes')
    parser.add_argument('--directory', type=Path, default=RELEASE, help='Explicit release directory with audited manifest and three inputs')
    args = parser.parse_args()
    try:
        merge(directory=args.directory, check=args.check)
    except (ValueError, OSError, KeyError, StopIteration, importlib.metadata.PackageNotFoundError) as error:
        parser.exit(1, f'Public merge rejected: {error}\n')


if __name__ == '__main__':
    main()
