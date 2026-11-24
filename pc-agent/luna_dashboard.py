"""Independent cached collectors; BLE reads never wait on PDH or App Server."""
from __future__ import annotations

import copy
import logging
import threading
import time

from codex_adapter import CodexAdapter, _remaining_percent
from luna_ble_music import bounded_text
from windows_dashboard import WindowsDashboard, finite

LOG = logging.getLogger("luna.ble.link")


def quota_windows(snapshot):
    windows = snapshot.get("codex", {}).get("windows", [])
    by_duration = {w.get("windowDurationMins"): w for w in windows if isinstance(w, dict)
                   and type(w.get("windowDurationMins")) is int}
    def remaining(duration):
        value = _remaining_percent(by_duration.get(duration))
        return value if value >= 0 else None
    return {"primary_remaining": remaining(300), "weekly_remaining": remaining(10080),
            "sampled_at_ms": snapshot.get("sampled_at_ms", 0)}


class DashboardService:
    def __init__(self, collector=None, codex=None):
        self.collector = collector
        self.codex = codex or CodexAdapter()
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._cached = {}
        self._sampled = 0
        self._thread = None

    def start(self):
        if self._thread is not None or self._stop.is_set():
            return
        self.codex.start()
        self._thread = threading.Thread(target=self._loop, name="luna-pc-metrics", daemon=True)
        self._thread.start()

    def _loop(self):
        retry_delay = 2
        sample_failures = 0
        try:
            while not self._stop.is_set():
                if self.collector is None:
                    try:
                        self.collector = WindowsDashboard()
                    except Exception as error:
                        LOG.warning("PC collector unavailable: %s; retry in %ss",
                                    type(error).__name__, retry_delay)
                        self._stop.wait(retry_delay)
                        retry_delay = min(30, retry_delay * 2)
                        continue
                try:
                    sample = self.collector.sample()
                    sample_failures = 0
                    retry_delay = 2
                except Exception as error:
                    LOG.warning("PC metric sample unavailable: %s", type(error).__name__)
                    sample = {}
                    sample_failures += 1
                with self._lock:
                    self._cached = sample
                    self._sampled = time.monotonic()
                if sample_failures >= 3:
                    collector, self.collector = self.collector, None
                    try:
                        collector.close()
                    except Exception as error:
                        LOG.warning("PC collector close failed: %s", type(error).__name__)
                    sample_failures = 0
                    self._stop.wait(retry_delay)
                    retry_delay = min(30, retry_delay * 2)
                else:
                    self._stop.wait(2)
        finally:
            if self.collector is not None:
                try:
                    self.collector.close()
                except Exception as error:
                    LOG.warning("PC collector close failed: %s", type(error).__name__)

    def snapshot(self):
        with self._lock:
            pc = copy.deepcopy(self._cached)
            age = int((time.monotonic()-self._sampled)*1000) if self._sampled else 60000
        computer = {key: finite(pc.get(key), 0, 65536 if key.endswith("_gb") else 150 if key.endswith("_temp") else 100)
                    for key in ("cpu", "gpu", "cpu_temp", "gpu_temp", "ram_used_gb", "ram_total_gb", "vram_used_gb", "vram_total_gb")}
        computer.update(name=bounded_text(pc.get("name"), 127), sample_age_ms=min(60000, age))
        return {"computer": computer, "quota": quota_windows(self.codex.snapshot()),
                "project": {"name": bounded_text(pc.get("project"), 191)}}

    def close(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)
        self.codex.close()
