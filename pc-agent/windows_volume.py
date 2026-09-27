"""Read and control the default Windows output endpoint without extra packages."""

from __future__ import annotations

import ctypes
import platform
import uuid
from contextlib import contextmanager
from typing import Iterator


class GUID(ctypes.Structure):
    _fields_ = [
        ("data1", ctypes.c_uint32),
        ("data2", ctypes.c_uint16),
        ("data3", ctypes.c_uint16),
        ("data4", ctypes.c_ubyte * 8),
    ]


def _guid(value: str) -> GUID:
    return GUID.from_buffer_copy(uuid.UUID(value).bytes_le)


_CLSID_DEVICE_ENUMERATOR = _guid("BCDE0395-E52F-467C-8E3D-C4579291692E")
_IID_DEVICE_ENUMERATOR = _guid("A95664D2-9614-4F35-A746-DE8DB63617E6")
_IID_ENDPOINT_VOLUME = _guid("5CDF2C82-841E-4546-9722-0CF74078229A")
_RPC_E_CHANGED_MODE = 0x80010106


def _check(hr: int) -> None:
    if hr < 0:
        raise OSError(f"Windows audio COM error 0x{hr & 0xFFFFFFFF:08X}")


def _method(interface: ctypes.c_void_p, index: int, *argtypes: type) -> object:
    table = ctypes.cast(interface, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    return ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, *argtypes)(table[index])


def _release(interface: ctypes.c_void_p) -> None:
    if interface.value:
        _method(interface, 2)(interface)


@contextmanager
def _default_output_volume() -> Iterator[ctypes.c_void_p]:
    if platform.system() != "Windows":
        raise RuntimeError("Windows output volume is only available on Windows")
    ole32 = ctypes.windll.ole32
    ole32.CoInitializeEx.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    ole32.CoInitializeEx.restype = ctypes.c_long
    ole32.CoCreateInstance.argtypes = [
        ctypes.POINTER(GUID), ctypes.c_void_p, ctypes.c_ulong,
        ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p),
    ]
    ole32.CoCreateInstance.restype = ctypes.c_long
    initialized = ole32.CoInitializeEx(None, 0)
    if initialized < 0 and (initialized & 0xFFFFFFFF) != _RPC_E_CHANGED_MODE:
        _check(initialized)

    enumerator = ctypes.c_void_p()
    device = ctypes.c_void_p()
    volume = ctypes.c_void_p()
    try:
        _check(ole32.CoCreateInstance(
            ctypes.byref(_CLSID_DEVICE_ENUMERATOR), None, 1,
            ctypes.byref(_IID_DEVICE_ENUMERATOR), ctypes.byref(enumerator),
        ))
        # IMMDeviceEnumerator: IUnknown(0-2), EnumAudioEndpoints(3), GetDefaultAudioEndpoint(4).
        _check(_method(enumerator, 4, ctypes.c_int, ctypes.c_int,
                       ctypes.POINTER(ctypes.c_void_p))(
                           enumerator, 0, 1, ctypes.byref(device)))
        # IMMDevice::Activate is the first method after IUnknown.
        _check(_method(device, 3, ctypes.POINTER(GUID), ctypes.c_ulong,
                       ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))(
                           device, ctypes.byref(_IID_ENDPOINT_VOLUME), 1, None,
                           ctypes.byref(volume)))
        yield volume
    finally:
        _release(volume)
        _release(device)
        _release(enumerator)
        if initialized in (0, 1):
            ole32.CoUninitialize()


def _read(volume: ctypes.c_void_p) -> dict[str, object]:
    level = ctypes.c_float()
    muted = ctypes.c_int()
    # IAudioEndpointVolume: GetMasterVolumeLevelScalar(9), GetMute(15).
    _check(_method(volume, 9, ctypes.POINTER(ctypes.c_float))(
        volume, ctypes.byref(level)))
    _check(_method(volume, 15, ctypes.POINTER(ctypes.c_int))(
        volume, ctypes.byref(muted)))
    return {
        "available": True,
        "percent": max(0, min(100, round(level.value * 100))),
        "muted": bool(muted.value),
    }


class WindowsVolumeAdapter:
    def snapshot(self) -> dict[str, object]:
        try:
            with _default_output_volume() as volume:
                return _read(volume)
        except (OSError, RuntimeError):
            return {"available": False, "percent": 0, "muted": False}

    def set_percent(self, percent: int) -> dict[str, object]:
        if isinstance(percent, bool) or not isinstance(percent, int) or not 0 <= percent <= 100:
            raise ValueError("volume percent must be an integer from 0 to 100")
        with _default_output_volume() as volume:
            # IAudioEndpointVolume::SetMasterVolumeLevelScalar is vtable entry 7.
            _check(_method(volume, 7, ctypes.c_float, ctypes.c_void_p)(
                volume, percent / 100.0, None))
            return _read(volume)

    def toggle_mute(self) -> dict[str, object]:
        with _default_output_volume() as volume:
            before = _read(volume)
            # IAudioEndpointVolume::SetMute is vtable entry 14.
            _check(_method(volume, 14, ctypes.c_int, ctypes.c_void_p)(
                volume, not before["muted"], None))
            return _read(volume)
