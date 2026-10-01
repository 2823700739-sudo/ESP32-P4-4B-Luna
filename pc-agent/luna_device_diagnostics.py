"""Validate the small, versioned runtime snapshot published by Luna firmware."""

from __future__ import annotations

import json
import re
from typing import Any


USB_COUNTERS = (
    "cdc_opens", "handshakes", "exchanges_ok", "exchanges_failed",
    "exchanges_timed_out", "protocol_errors", "queue_drops", "tx_errors",
)
MEMORY_FIELDS = (
    "internal_free_bytes", "internal_min_free_bytes", "internal_largest_free_bytes",
    "psram_free_bytes", "psram_min_free_bytes",
)
# Values from the pinned ESP-IDF 6.0.2 esp_reset_reason_t.
RESET_REASONS = (
    "unknown", "power_on", "external_pin", "software", "panic", "interrupt_watchdog",
    "task_watchdog", "watchdog", "deep_sleep", "brownout", "sdio", "usb", "jtag",
    "efuse", "power_glitch", "cpu_lockup",
)


def parse_device_diagnostics(payload: bytes) -> dict[str, Any]:
    if len(payload) > 1024:
        raise ValueError("device diagnostics payload is too large")
    body = json.loads(payload.decode("utf-8"))
    if not isinstance(body, dict) or type(body.get("schema")) is not int or body["schema"] != 1:
        raise ValueError("unsupported device diagnostics schema")

    def integer(source: dict, name: str, maximum: int = 0xFFFFFFFF) -> int:
        value = source.get(name)
        if type(value) is not int or not 0 <= value <= maximum:
            raise ValueError(f"invalid device diagnostics field: {name}")
        return value

    result: dict[str, Any] = {"schema": 1}
    for name, length in (("boot_id", 16), ("firmware_elf_sha256", 64)):
        value = body.get(name)
        if not isinstance(value, str) or re.fullmatch(rf"[0-9a-fA-F]{{{length}}}", value) is None:
            raise ValueError(f"invalid device diagnostics field: {name}")
        result[name] = value.lower()
    result["uptime_seconds"] = integer(body, "uptime_seconds", 0xFFFFFFFFFFFFFFFF)
    reason = integer(body, "reset_reason", 255)
    result["reset_reason"] = reason
    result["reset_reason_name"] = RESET_REASONS[reason] if reason < len(RESET_REASONS) else "unknown"
    if type(body.get("wifi_online")) is not bool:
        raise ValueError("invalid device diagnostics field: wifi_online")
    result["wifi_online"] = body["wifi_online"]
    for name in MEMORY_FIELDS:
        result[name] = integer(body, name)
    counters = body.get("usb")
    if not isinstance(counters, dict):
        raise ValueError("invalid device diagnostics USB counters")
    result["usb"] = {name: integer(counters, name) for name in USB_COUNTERS}
    return result
