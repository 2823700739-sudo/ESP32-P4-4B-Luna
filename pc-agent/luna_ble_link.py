"""Persistent BLE link. Optional B1 music, never COM/HTTP/cover business."""
from __future__ import annotations

import argparse
import asyncio
import faulthandler
from datetime import datetime
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import sys
import time

from agent_instance import single_agent_instance
from luna_ble_probe import connected_session, is_luna

LOG = logging.getLogger("luna.ble.link")


async def discover_device(args):
    from bleak import BleakScanner
    devices = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    candidates = [device for device, adv in devices.values() if is_luna(adv) and
                  (not args.address or device.address.lower() == args.address.lower())]
    if not candidates: raise ConnectionError("Luna not advertising")
    if len(candidates) != 1: raise RuntimeError("Multiple Luna devices; select --address")
    return candidates[0]


async def sync_time(session):
    offset = int(datetime.now().astimezone().utcoffset().total_seconds() // 60)
    await session.request("time_sync", epoch_ms=int(time.time() * 1000), timezone_offset_minutes=offset)


async def wait_stop(stop, seconds):
    try: await asyncio.wait_for(stop.wait(), timeout=seconds)
    except asyncio.TimeoutError: pass


async def supervise(args, stop):
    backoff = 2
    while not stop.is_set():
        connected_at = None
        phase = 'discovery'
        try:
            device = await discover_device(args)
            phase = 'connect/security'
            music = getattr(args, "music_service", None)
            if music:
                from luna_ble_music import MusicSession
                connection = connected_session(device, session_factory=MusicSession)
            else:
                connection = connected_session(device)
            async with connection as session:
                phase = 'hello/time_sync'
                hello = await session.request("hello")
                dashboard = getattr(args, "dashboard_service", None)
                dashboard_enabled = dashboard is not None and "dashboard" in (hello or {}).get("features", [])
                last_dashboard = last_dashboard_report = 0
                await sync_time(session)
                connected_at = time.monotonic()
                last_sync = last_report = connected_at
                count = 0
                LOG.info("CONNECTED: authenticated hello + time sync; ATT value=%d bytes; staying connected", session.att_payload)
                phase = 'business/heartbeat'
                while not stop.is_set():
                    # Each new connection has a fresh session and handshake.
                    # A failed request is never retried or replayed in that session.
                    if getattr(session, "next_id", 1) >= 65000:
                        raise ConnectionError("Renew session before message ID exhaustion")
                    action = await music.step(session) if music else False
                    if not music: await session.request("ping")
                    # Dispatch music first. Large read-only telemetry is capped at
                    # one packet set / 2 seconds and skipped on action iterations.
                    if dashboard_enabled and not action and time.monotonic()-last_dashboard >= 2:
                        try:
                            snapshot = dashboard.snapshot()
                        except Exception as error:
                            # A local read-only collector failure must not tear down
                            # the authenticated link or interrupt music control.
                            LOG.warning("Dashboard snapshot unavailable: %s", type(error).__name__)
                            last_dashboard = time.monotonic()
                        else:
                            # A failed BLE request still exits this session; never
                            # retry or replay a request after an uncertain send.
                            received = await session.request("dashboard_snapshot", **snapshot)
                            last_dashboard = time.monotonic()
                            if last_dashboard-last_dashboard_report >= 60:
                                LOG.info("DASHBOARD accepted: CPU=%s GPU=%s quota5h=%s quotaWeek=%s",
                                         received.get("cpu"), received.get("gpu"), received.get("primary_remaining"), received.get("weekly_remaining"))
                                last_dashboard_report = last_dashboard
                    count += 1
                    now = time.monotonic()
                    if now - last_sync >= args.sync_seconds:
                        await sync_time(session)
                        last_sync = time.monotonic()
                    if now - last_report >= 30:
                        LOG.info("HEALTHY: %d exchanges; connected for %.0f seconds", count, now-connected_at)
                        last_report = now
                    await wait_stop(stop, .25 if music else args.heartbeat_seconds)
        except asyncio.CancelledError:
            raise
        except Exception as error:
            # Exception text can include a peer address. Keep logs free of it.
            LOG.warning("OFFLINE: %s phase=%s; no request replay; retry in %d seconds", type(error).__name__, phase, backoff)
        if stop.is_set(): break
        if connected_at is not None and time.monotonic()-connected_at >= 30: backoff = 2
        await wait_stop(stop, backoff)
        backoff = min(backoff * 2, 30)


async def run(args):
    if sys.platform == "win32" and sys.getwindowsversion().build < 22000:
        raise RuntimeError("B0 requires Windows 11 build 22000 or newer")
    task = asyncio.create_task(supervise(args, asyncio.Event()))
    try:
        if args.run_seconds:
            await asyncio.wait_for(asyncio.shield(task), timeout=args.run_seconds)
        else:
            await task
    except asyncio.TimeoutError:
        LOG.info("Bounded test duration ended; disconnecting intentionally")
    finally:
        task.cancel()
        try: await task
        except asyncio.CancelledError: pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", help="Select amongst multiple Luna devices; not authentication")
    parser.add_argument("--scan-seconds", type=float, default=5)
    parser.add_argument("--heartbeat-seconds", type=float, default=10)
    parser.add_argument("--sync-seconds", type=float, default=60)
    parser.add_argument("--run-seconds", type=float, default=0, help="Bounded test only; default stays running")
    parser.add_argument("--background", action="store_true", help="Write rotating ble-link.log instead of console")
    parser.add_argument("--music", action="store_true", help="Enable B1 music/volume, requires B1 firmware and requirements-ble-music.txt")
    args = parser.parse_args()
    if not (1 <= args.scan_seconds <= 30 and 1 <= args.heartbeat_seconds <= 60 and
            10 <= args.sync_seconds <= 3600 and 0 <= args.run_seconds <= 86400):
        parser.error("Invalid scan/heartbeat/sync/test duration")
    handler = (RotatingFileHandler(Path(__file__).with_name("ble-link.log"), maxBytes=262144,
                                  backupCount=2, encoding="utf-8")
               if args.background else logging.StreamHandler(sys.stdout))
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    LOG.addHandler(handler)
    LOG.setLevel(logging.INFO)
    with single_agent_instance("Local\\LunaBleLink") as acquired:
        if not acquired:
            LOG.info("Luna BLE link is already running")
            return 0
        LOG.info("Luna BLE resident starting; no serial ports opened; manual pairing only")
        media = None
        dashboard = None
        crash_stream = None
        try:
            try:
                crash_stream = Path(__file__).with_name('ble-crash.log').open('a', encoding='utf-8')
                faulthandler.enable(file=crash_stream, all_threads=True)
            except OSError:
                LOG.warning('Native crash diagnostics unavailable; BLE can still run')
            if args.music:
                from windows_media import WindowsMediaAdapter
                from windows_volume import WindowsVolumeAdapter
                from luna_ble_music import MusicService
                media = WindowsMediaAdapter()
                media.start()
                args.music_service = MusicService(media, WindowsVolumeAdapter())
                from luna_dashboard import DashboardService
                dashboard = DashboardService()
                dashboard.start()
                args.dashboard_service = dashboard
                LOG.info("B1 music enabled; thumbnail streams disabled; no media-key fallback")
            asyncio.run(run(args))
        except KeyboardInterrupt: LOG.info("Stopped by user; disconnected")
        except Exception as error:
            LOG.error("Stopped: %s", type(error).__name__)
            return 1
        finally:
            if dashboard is not None: dashboard.close()
            if media is not None: media.close()
            if crash_stream is not None:
                faulthandler.disable()
                crash_stream.close()
    return 0


if __name__ == "__main__": raise SystemExit(main())
