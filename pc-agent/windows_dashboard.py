"""Read-only Windows metrics. No driver, elevation, network or serial port.

CPU = total Processor Utility (performance-state aware); RAM = physical used memory.
GPU = busiest WDDM engine on the largest dedicated-memory DXGI adapter.
VRAM = adapter-wide dedicated usage, NOT summed GPU Process Memory counters.
Unsupported temperatures remain None; never substitute ACPI system temperature.
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import math
import logging
import re
import sys
import time
import uuid

LOG = logging.getLogger("luna.ble.link")
PDH_MORE_DATA = 0x800007D2


def finite(value, low=0, high=100):
    return round(value, 1) if type(value) in (int, float) and math.isfinite(value) and low <= value <= high else None


def engine_usage(rows, luid):
    engines = {}
    for name, value in rows:
        if not isinstance(name, str) or luid not in name.lower():
            continue
        match = re.search(r"phys_(\d+)_eng_(\d+)_", name)
        value = finite(value)
        if match and value is not None:
            key = match.groups()
            engines[key] = engines.get(key, 0) + value
    return round(min(100, max(engines.values())), 1) if engines else None


def vscode_project(title):
    # Default VS Code window title only. Customized/ambiguous titles are unknown.
    if not title.endswith(" - Visual Studio Code"):
        return ""
    parts = title[:-len(" - Visual Studio Code")].split(" - ")
    if len(parts) < 2:
        return ""  # A lone untitled/file window isn't a workspace.
    name = parts[-1].strip()
    return name if name not in ("Untitled", "无标题") else ""


class Memory(C.Structure):
    _fields_ = [("length", W.DWORD), ("load", W.DWORD)] + [(key, C.c_ulonglong) for key in
        ("total", "available", "page_total", "page_available", "virtual_total", "virtual_available", "extended")]


class Guid(C.Structure):
    _fields_ = [("data1", W.DWORD), ("data2", W.WORD), ("data3", W.WORD), ("data4", C.c_ubyte * 8)]


class AdapterDesc(C.Structure):
    _fields_ = [("name", W.WCHAR * 128)] + [(key, W.UINT) for key in ("vendor", "device", "subsystem", "revision")] + [
        (key, C.c_size_t) for key in ("dedicated", "system", "shared")] + [("low", W.DWORD), ("high", W.LONG), ("flags", W.UINT)]


def com_call(obj, index, result, args, *values):
    table = C.cast(obj, C.POINTER(C.POINTER(C.c_void_p))).contents
    return C.WINFUNCTYPE(result, C.c_void_p, *args)(table[index])(obj, *values)


def main_gpu():
    iid = Guid.from_buffer_copy(uuid.UUID("770aae78-f26f-4dba-a829-253c83d1b387").bytes_le)
    dll = C.WinDLL("dxgi")
    dll.CreateDXGIFactory1.argtypes = [C.POINTER(Guid), C.POINTER(C.c_void_p)]
    dll.CreateDXGIFactory1.restype = W.LONG
    factory = C.c_void_p()
    if dll.CreateDXGIFactory1(C.byref(iid), C.byref(factory)) != 0:
        return None
    adapters = []
    try:
        for index in range(16):
            adapter = C.c_void_p()
            if com_call(factory, 12, W.LONG, [W.UINT, C.POINTER(C.c_void_p)], index, C.byref(adapter)) != 0:
                break
            try:
                desc = AdapterDesc()
                if com_call(adapter, 10, W.LONG, [C.POINTER(AdapterDesc)], C.byref(desc)) == 0 and not desc.flags & 2:
                    adapters.append({"name": desc.name, "total": desc.dedicated,
                        "luid": f"luid_0x{desc.high & 0xffffffff:08x}_0x{desc.low:08x}"})
            finally:
                com_call(adapter, 2, W.ULONG, [])
    finally:
        com_call(factory, 2, W.ULONG, [])
    return max(adapters, key=lambda item: item["total"]) if adapters else None


class CounterNumber(C.Union):
    _fields_ = [("number", C.c_double), ("large", C.c_longlong), ("long", W.LONG)]


class CounterValue(C.Structure):
    _anonymous_ = ("value",)
    _fields_ = [("status", W.DWORD), ("value", CounterNumber)]


class CounterItem(C.Structure):
    _fields_ = [("name", W.LPWSTR), ("value", CounterValue)]


class CpuCounter:
    """Independent total CPU utility query, never a scaled busy-time estimate."""
    def __init__(self):
        self.dll = C.WinDLL("pdh")
        self.query = C.c_void_p()
        self.counter = C.c_void_p()
        self.samples = 0
        self.dll.PdhOpenQueryW.argtypes = [W.LPCWSTR, C.c_size_t, C.POINTER(C.c_void_p)]
        self.dll.PdhAddEnglishCounterW.argtypes = [C.c_void_p, W.LPCWSTR, C.c_size_t, C.POINTER(C.c_void_p)]
        self.dll.PdhCollectQueryData.argtypes = [C.c_void_p]
        self.dll.PdhGetFormattedCounterValue.argtypes = [C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(CounterValue)]
        self.dll.PdhCloseQuery.argtypes = [C.c_void_p]
        for name in ("PdhOpenQueryW", "PdhAddEnglishCounterW", "PdhCollectQueryData",
                     "PdhGetFormattedCounterValue", "PdhCloseQuery"):
            getattr(self.dll, name).restype = W.DWORD
        if self.dll.PdhOpenQueryW(None, 0, C.byref(self.query)):
            self.close()
            raise OSError("CPU query unavailable")
        try:
            if self.dll.PdhAddEnglishCounterW(self.query,
                    r"\Processor Information(_Total)\% Processor Utility", 0, C.byref(self.counter)):
                raise OSError("CPU utility counter unavailable")
            if self.dll.PdhCollectQueryData(self.query):
                raise OSError("CPU utility warmup unavailable")
        except Exception:
            self.close()
            raise

    def collect(self):
        if self.dll.PdhCollectQueryData(self.query):
            raise OSError("CPU utility collection unavailable")
        self.samples += 1
        value = CounterValue()
        # Read the actual utility before limiting the product's 0..100 display.
        rc = self.dll.PdhGetFormattedCounterValue(self.counter, 0x200 | 0x8000,
                                                 None, C.byref(value))
        if rc or value.status not in (0, 1):
            # The first two samples may share an instant during startup.
            if self.samples == 1 and (rc == 0xC0000BC6 or (rc == 0 and value.status == 0xC0000BBA)):
                return None
            raise OSError("CPU utility data unavailable")
        raw = value.number
        return round(min(100, raw), 1) if math.isfinite(raw) and raw >= 0 else None

    def close(self):
        if self.query:
            self.dll.PdhCloseQuery(self.query)
            self.query = None


class GpuCounters:
    def __init__(self):
        self.dll = C.WinDLL("pdh")
        self.query = C.c_void_p()
        self.counters = {}
        self.dll.PdhOpenQueryW.argtypes = [W.LPCWSTR, C.c_size_t, C.POINTER(C.c_void_p)]
        self.dll.PdhAddEnglishCounterW.argtypes = [C.c_void_p, W.LPCWSTR, C.c_size_t, C.POINTER(C.c_void_p)]
        self.dll.PdhCollectQueryData.argtypes = [C.c_void_p]
        self.dll.PdhCloseQuery.argtypes = [C.c_void_p]
        self.dll.PdhGetFormattedCounterArrayW.argtypes = [C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(W.DWORD), C.c_void_p]
        self.dll.PdhGetFormattedCounterArrayW.restype = W.DWORD
        if self.dll.PdhOpenQueryW(None, 0, C.byref(self.query)):
            raise OSError("PDH query unavailable")
        try:
            for key, path in (("engines", r"\GPU Engine(*)\Utilization Percentage"),
                              ("memory", r"\GPU Adapter Memory(*)\Dedicated Usage")):
                counter = C.c_void_p()
                if self.dll.PdhAddEnglishCounterW(self.query, path, 0, C.byref(counter)) == 0:
                    self.counters[key] = counter
            if not self.counters:
                raise OSError("GPU counters unavailable")
            self.dll.PdhCollectQueryData(self.query)
        except Exception:
            self.close()
            raise

    def collect(self):
        if self.dll.PdhCollectQueryData(self.query):
            raise OSError("PDH collection unavailable")
        result = {}
        array_failed = False
        for key, counter in self.counters.items():
            size, count = W.DWORD(), W.DWORD()
            status = self.dll.PdhGetFormattedCounterArrayW(counter, 0x200, C.byref(size), C.byref(count), None)
            if status not in (0, PDH_MORE_DATA):
                array_failed = True
                continue
            if status == 0 and size.value == 0 and count.value == 0:
                result[key] = []
                continue
            if not 0 < size.value <= 4 * 1024 * 1024:
                continue
            buffer = C.create_string_buffer(size.value)
            if self.dll.PdhGetFormattedCounterArrayW(counter, 0x200, C.byref(size), C.byref(count), buffer):
                array_failed = True
                continue
            if count.value > len(buffer) // C.sizeof(CounterItem):
                continue
            items = C.cast(buffer, C.POINTER(CounterItem))
            result[key] = [(items[i].name, items[i].value.number) for i in range(count.value)
                           if items[i].name and items[i].value.status in (0, 1)
                           and math.isfinite(items[i].value.number)]
        if array_failed and not result:
            # Collection can succeed while every formatted counter is broken.
            # Reuse the caller's bounded GPU recovery instead of retrying forever.
            raise OSError("PDH formatted arrays unavailable")
        return result

    def close(self):
        if self.query:
            self.dll.PdhCloseQuery(self.query)
            self.query = None


class WindowsDashboard:
    def __init__(self):
        if sys.platform != "win32":
            raise RuntimeError("Windows metrics only")
        self.kernel = C.WinDLL("kernel32", use_last_error=True)
        self.kernel.GlobalMemoryStatusEx.argtypes = [C.POINTER(Memory)]
        self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.kernel.OpenProcess.restype = C.c_void_p
        self.kernel.QueryFullProcessImageNameW.argtypes = [C.c_void_p, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
        self.kernel.CloseHandle.argtypes = [C.c_void_p]
        self.user = C.WinDLL("user32")
        self.user.GetForegroundWindow.restype = W.HWND
        self.user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        self.user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        self.project = ""
        self.cpu_counter = None
        self._cpu_retry_at = 0
        self.gpu = None
        self.counters = None
        self._gpu_retry_at = 0
        self._ensure_cpu()
        self._ensure_gpu()

    def _ensure_cpu(self):
        now = time.monotonic()
        if now < self._cpu_retry_at:
            return
        self._cpu_retry_at = now + 30
        try:
            self.cpu_counter = CpuCounter()
        except OSError:
            LOG.debug("CPU utility temporarily unavailable")

    def _cpu_value(self):
        if self.cpu_counter is None:
            self._ensure_cpu()
        if self.cpu_counter is None:
            return None
        try:
            return self.cpu_counter.collect()
        except OSError:
            LOG.warning("CPU utility unavailable; retry in 30s")
            counter, self.cpu_counter = self.cpu_counter, None
            self._cpu_retry_at = time.monotonic() + 30
            try:
                counter.close()
            except OSError:
                LOG.debug("Failed CPU query already unavailable")
            return None

    def _ensure_gpu(self):
        now = time.monotonic()
        if now < self._gpu_retry_at:
            return
        self._gpu_retry_at = now + 30
        # Each provider can fail independently, particularly around wake/driver reset.
        if self.gpu is None:
            try:
                self.gpu = main_gpu()
            except OSError:
                LOG.debug("DXGI adapter temporarily unavailable")
        if self.counters is None:
            try:
                self.counters = GpuCounters()
            except OSError:
                LOG.debug("GPU counters temporarily unavailable")

    def _gpu_values(self):
        if self.gpu is None or self.counters is None:
            self._ensure_gpu()
        if self.counters is None:
            return {}
        try:
            return self.counters.collect()
        except OSError:
            LOG.warning("GPU counters unavailable; retry in 30s")
            counters, self.counters = self.counters, None
            self.gpu = None  # Re-enumerate the adapter as well after a driver reset.
            self._gpu_retry_at = time.monotonic() + 30
            try:
                counters.close()
            except OSError:
                LOG.debug("Failed GPU query already unavailable")
            return {}

    def read_project(self):
        hwnd, pid = self.user.GetForegroundWindow(), W.DWORD()
        self.user.GetWindowThreadProcessId(hwnd, C.byref(pid))
        process = self.kernel.OpenProcess(0x1000, False, pid)
        if not process:
            return self.project
        try:
            path, size = C.create_unicode_buffer(32768), W.DWORD(32768)
            if self.kernel.QueryFullProcessImageNameW(process, 0, path, C.byref(size)) and path.value.lower().endswith("\\code.exe"):
                title = C.create_unicode_buffer(2048)
                self.user.GetWindowTextW(hwnd, title, len(title))
                # Keep last edited VS Code project when another app is foreground.
                self.project = vscode_project(title.value)
        finally:
            self.kernel.CloseHandle(process)
        return self.project

    def sample(self):
        cpu = self._cpu_value()
        memory = Memory(); memory.length = C.sizeof(memory)
        ram_used = ram_total = None
        if self.kernel.GlobalMemoryStatusEx(C.byref(memory)):
            ram_total, ram_used = memory.total, memory.total-memory.available
        values = self._gpu_values()
        gpu = vram_used = vram_total = None
        name = "GPU 不可用"
        if self.gpu:
            name, luid, vram_total = self.gpu["name"], self.gpu["luid"], self.gpu["total"]
            gpu = engine_usage(values.get("engines", []), luid)
            rows = [value for key, value in values.get("memory", [])
                    if isinstance(key, str) and luid in key.lower()
                    and type(value) in (int, float) and math.isfinite(value) and value >= 0]
            if rows:
                measured = sum(rows)
                # A duplicated or malformed PDH row must not make the firmware
                # reject the entire dashboard snapshot as used > total.
                if vram_total > 0 and measured <= vram_total:
                    vram_used = measured
        gib = 1024 ** 3
        return {"cpu": cpu, "gpu": gpu, "cpu_temp": None, "gpu_temp": None,
            "ram_used_gb": round(ram_used/gib, 2) if ram_used is not None else None,
            "ram_total_gb": round(ram_total/gib, 2) if ram_total else None,
            "vram_used_gb": round(vram_used/gib, 2) if vram_used is not None else None,
            "vram_total_gb": round(vram_total/gib, 2) if vram_total else None,
            "name": name, "project": self.read_project()}

    def close(self):
        cpu, self.cpu_counter = self.cpu_counter, None
        gpu, self.counters = self.counters, None
        try:
            if cpu:
                cpu.close()
        finally:
            if gpu:
                gpu.close()
