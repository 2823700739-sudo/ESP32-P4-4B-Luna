"""Read-only Windows Agent prerequisites; no BLE scan, pairing or media actions."""
from __future__ import annotations

import importlib
import importlib.metadata
from pathlib import Path
import platform
import struct
import sys


def requirements(path, visited=None):
    visited = set() if visited is None else visited
    path = path.resolve()
    if path in visited:
        return {}
    visited.add(path)
    result = {}
    for raw in path.read_text(encoding='utf-8').splitlines():
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('-r '):
            result.update(requirements(path.parent / line[3:].strip(), visited))
        else:
            name, version = line.split('==')
            result[name] = version
    return result


def main():
    failures = []
    if sys.platform != 'win32' or sys.getwindowsversion().build < 22000:
        failures.append('Windows 11 build >=22000 required')
    if sys.version_info[:2] not in {(3, 10), (3, 11)} or struct.calcsize('P') != 8 or platform.machine().upper() not in {'AMD64', 'X86_64'}:
        failures.append('Use 64-bit x64 Python 3.11 for new installations (legacy 3.10 also recognized)')
    directory = Path(__file__).resolve().parent
    for name, expected in requirements(directory / 'requirements-ble-music.txt').items():
        try:
            actual = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            actual = 'missing'
        if actual != expected:
            failures.append(f'{name}: expected {expected}, got {actual}')
    if not failures:
        for name in ['bleak', 'winrt.windows.media.control', 'windows_media',
                     'windows_volume', 'windows_dashboard', 'luna_ble_music', 'luna_dashboard']:
            try:
                importlib.import_module(name)
            except Exception as error:
                failures.append(f'{name}: import failed ({type(error).__name__})')
    if failures:
        print('Agent prerequisites FAILED:')
        for failure in failures:
            print('- ' + failure)
        return 1
    print(f'Agent prerequisites OK: Windows 11, x64 Python {sys.version_info.major}.{sys.version_info.minor}, pinned packages and imports.')
    if sys.version_info[:2] == (3, 10):
        print('Legacy Python 3.10 environment recognized; use Python 3.11 for new installations.')
    print('No Bluetooth scan, pairing, tasks, account access or media actions performed.')
    print('This does not verify adapter availability, C6 compatibility or an actual Luna connection.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
