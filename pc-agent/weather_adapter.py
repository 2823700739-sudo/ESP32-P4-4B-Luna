"""Cached Open-Meteo adapter for Luna's configured location."""

from __future__ import annotations

import copy
import json
import math
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, urlopen


FORECAST_URL = "https://api.open-meteo.com/v1/forecast"

WMO_CONDITIONS = {
    0: "晴朗",
    1: "晴间多云",
    2: "多云",
    3: "阴",
    45: "雾",
    48: "雾凇",
    51: "小毛毛雨",
    53: "毛毛雨",
    55: "强毛毛雨",
    56: "小冻毛毛雨",
    57: "冻毛毛雨",
    61: "小雨",
    63: "中雨",
    65: "大雨",
    66: "小冻雨",
    67: "冻雨",
    71: "小雪",
    73: "中雪",
    75: "大雪",
    77: "雪粒",
    80: "小阵雨",
    81: "阵雨",
    82: "强阵雨",
    85: "小阵雪",
    86: "强阵雪",
    95: "雷暴",
    96: "雷暴伴小冰雹",
    99: "雷暴伴冰雹",
}


def _number(value: Any, fallback: float = 0.0) -> float:
    if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
        return float(value)
    return fallback


def _first(values: Any, fallback: float = 0.0) -> float:
    if isinstance(values, list) and values:
        return _number(values[0], fallback)
    return fallback


def _display_time(value: str) -> str:
    if len(value) >= 16 and "T" in value:
        return value[5:10] + " " + value[11:16]
    return value[:16]


def _coordinate(value: float, positive: str, negative: str) -> str:
    return f"{abs(value):.4f} {positive if value >= 0 else negative}"


class WeatherAdapter:
    """Refresh weather in the background and retain the last successful snapshot."""

    def __init__(self, config: dict[str, Any], cache_path: Path) -> None:
        self.location = str(config.get("location") or "").strip()
        self.latitude = self._coordinate_setting(config.get("latitude"), -90.0, 90.0)
        self.longitude = self._coordinate_setting(config.get("longitude"), -180.0, 180.0)
        self.refresh_seconds = max(300, int(config.get("refresh_minutes") or 20) * 60)
        self.cache_path = cache_path
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._snapshot = self._empty_snapshot()
        self._load_cache()

    @staticmethod
    def _coordinate_setting(value: Any, minimum: float, maximum: float) -> float | None:
        if isinstance(value, bool):
            return None
        try:
            parsed = float(value)
        except (TypeError, ValueError):
            return None
        return parsed if math.isfinite(parsed) and minimum <= parsed <= maximum else None

    @property
    def configured(self) -> bool:
        return bool(self.location) and self.latitude is not None and self.longitude is not None

    def _empty_snapshot(self, error: str = "Weather location is not configured") -> dict[str, Any]:
        return {
            "configured": self.configured,
            "available": False,
            "stale": False,
            "location": self.location or "Location not configured",
            "latitude": self.latitude,
            "longitude": self.longitude,
            "condition": "Unavailable",
            "temperature_c": -999,
            "apparent_temperature_c": -999,
            "humidity_percent": -1,
            "wind_speed_kmh": -1,
            "high_c": -999,
            "low_c": -999,
            "precipitation_probability_percent": -1,
            "observed_at": "",
            "fetched_at": "",
            "source": "Open-Meteo",
            "summary": self.location or "Location not configured",
            "details": error,
            "error": error,
        }

    def _format_display(self, snapshot: dict[str, Any]) -> None:
        snapshot["summary"] = (
            f"{snapshot['condition']}\n{snapshot['location']}\n"
            f"Feels {snapshot['apparent_temperature_c']} C | "
            f"Humidity {snapshot['humidity_percent']}%"
        )
        snapshot["details"] = (
            f"H {snapshot['high_c']} / L {snapshot['low_c']} C | "
            f"Rain {snapshot['precipitation_probability_percent']}%\n"
            f"Wind {snapshot['wind_speed_kmh']} km/h\n"
            f"{_coordinate(snapshot['latitude'], 'N', 'S')} | "
            f"{_coordinate(snapshot['longitude'], 'E', 'W')}\n"
            f"Updated {_display_time(snapshot['observed_at'])}"
        )

    def _load_cache(self) -> None:
        if not self.configured or not self.cache_path.is_file():
            return
        try:
            cached = json.loads(self.cache_path.read_text(encoding="utf-8"))
            if not isinstance(cached, dict) or not cached.get("available"):
                return
            if (
                _number(cached.get("latitude"), 999.0) != self.latitude
                or _number(cached.get("longitude"), 999.0) != self.longitude
            ):
                return
            cached["stale"] = True
            cached["error"] = ""
            self._format_display(cached)
            self._snapshot = cached
        except (OSError, ValueError, TypeError):
            return

    def _save_cache(self, snapshot: dict[str, Any]) -> None:
        temporary = self.cache_path.with_suffix(self.cache_path.suffix + ".tmp")
        try:
            temporary.write_text(
                json.dumps(snapshot, ensure_ascii=False, separators=(",", ":")),
                encoding="utf-8",
            )
            temporary.replace(self.cache_path)
        except OSError as error:
            print(f"Weather cache write failed: {error}")

    def _fetch(self) -> dict[str, Any]:
        assert self.latitude is not None and self.longitude is not None
        query = urlencode(
            {
                "latitude": f"{self.latitude:.6f}",
                "longitude": f"{self.longitude:.6f}",
                "current": (
                    "temperature_2m,relative_humidity_2m,apparent_temperature,"
                    "weather_code,wind_speed_10m"
                ),
                "daily": (
                    "temperature_2m_max,temperature_2m_min,"
                    "precipitation_probability_max"
                ),
                "timezone": "auto",
                "forecast_days": "1",
            }
        )
        request = Request(f"{FORECAST_URL}?{query}", headers={"User-Agent": "Luna/0.2"})
        with urlopen(request, timeout=8) as response:
            payload = json.loads(response.read().decode("utf-8"))
        if not isinstance(payload, dict) or payload.get("error"):
            raise RuntimeError(str(payload.get("reason", "Invalid weather response")))
        current = payload.get("current")
        daily = payload.get("daily")
        if not isinstance(current, dict) or not isinstance(daily, dict):
            raise RuntimeError("Weather response is missing current or daily data")

        weather_code = round(_number(current.get("weather_code"), -1.0))
        snapshot = {
            "configured": True,
            "available": True,
            "stale": False,
            "location": self.location,
            "latitude": self.latitude,
            "longitude": self.longitude,
            "condition": WMO_CONDITIONS.get(weather_code, f"天气代码 {weather_code}"),
            "weather_code": weather_code,
            "temperature_c": round(_number(current.get("temperature_2m"), -999.0)),
            "apparent_temperature_c": round(
                _number(current.get("apparent_temperature"), -999.0)
            ),
            "humidity_percent": round(_number(current.get("relative_humidity_2m"), -1.0)),
            "wind_speed_kmh": round(_number(current.get("wind_speed_10m"), -1.0)),
            "high_c": round(_first(daily.get("temperature_2m_max"), -999.0)),
            "low_c": round(_first(daily.get("temperature_2m_min"), -999.0)),
            "precipitation_probability_percent": round(
                _first(daily.get("precipitation_probability_max"), -1.0)
            ),
            "observed_at": str(current.get("time") or ""),
            "fetched_at": datetime.now(timezone.utc)
            .isoformat(timespec="seconds")
            .replace("+00:00", "Z"),
            "source": "Open-Meteo",
            "error": "",
        }
        self._format_display(snapshot)
        return snapshot

    def _refresh_once(self) -> None:
        try:
            snapshot = self._fetch()
        except Exception as error:
            print(f"Weather refresh failed: {error}")
            with self._lock:
                if self._snapshot.get("available"):
                    self._snapshot["stale"] = True
                    self._snapshot["error"] = str(error)
                    self._format_display(self._snapshot)
                else:
                    self._snapshot = self._empty_snapshot(str(error))
            return
        with self._lock:
            self._snapshot = snapshot
        self._save_cache(snapshot)

    def _refresh_loop(self) -> None:
        while not self._stop_event.is_set():
            self._refresh_once()
            self._stop_event.wait(self.refresh_seconds)

    def start(self) -> None:
        if not self.configured or self._thread is not None:
            return
        self._thread = threading.Thread(
            target=self._refresh_loop, name="weather-refresh", daemon=True
        )
        self._thread.start()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return copy.deepcopy(self._snapshot)

    def close(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
