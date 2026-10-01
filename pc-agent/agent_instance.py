"""Keep one Luna Agent per interactive Windows session."""

from __future__ import annotations

import ctypes
import os
from contextlib import contextmanager
from collections.abc import Iterator


@contextmanager
def single_agent_instance(name: str = "Local\\LunaDesktopAgent") -> Iterator[bool]:
    if os.name != "nt":
        yield True
        return

    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateMutexW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR]
    kernel32.CreateMutexW.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL

    handle = kernel32.CreateMutexW(None, False, name)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    already_running = ctypes.get_last_error() == 183  # ERROR_ALREADY_EXISTS
    try:
        yield not already_running
    finally:
        kernel32.CloseHandle(handle)
