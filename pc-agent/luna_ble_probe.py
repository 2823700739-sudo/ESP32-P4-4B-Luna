"""Explicit B0 BLE diagnostic. Default: scan only, never COM/HTTP/media actions."""
from __future__ import annotations

import argparse
import asyncio
from contextlib import asynccontextmanager
from datetime import datetime
import json
import statistics
import sys
import time
import uuid

from luna_ble_protocol import DEVICE_NAME, SERVICE_UUID, RX_UUID, TX_UUID, READY_UUID, ProtocolError, Reassembler, encode_fragments, att_payload_for_mtu


def is_luna(advertisement) -> bool:
    # Name alone is neither unique nor authentication. Match our service too.
    return advertisement.local_name == DEVICE_NAME and SERVICE_UUID in [
        value.lower() for value in advertisement.service_uuids]


class ProbeSession:
    REQUEST_TYPES = {"hello": "hello", "ping": "pong", "time_sync": "time_sync_result", "diagnostic": "diagnostic", "time_status": "time_status"}

    def __init__(self, client, request_timeout: float = 8, att_payload_cap: int = 512):
        self.client = client
        self.session = uuid.uuid4().hex
        self.receiver = Reassembler()
        self.next_id = 1
        self.pending: dict[int, asyncio.Future] = {}
        self.lock = asyncio.Lock()
        self.boot_id: str | None = None
        self.request_timeout = request_timeout
        self.failed = False
        self.att_payload = 20
        self.offered_payload = att_payload_for_mtu(getattr(client, "mtu_size", None), att_payload_cap)

    def notification(self, _characteristic, packet: bytearray):
        try:
            message = self.receiver.feed(bytes(packet))
            if message is None: return
            result = json.loads(message.payload.decode("utf-8"))
            if not isinstance(result, dict) or result.get("v") != 1 or result.get("session") != self.session:
                raise ProtocolError("invalid reply/session")
            future = self.pending.get(message.message_id)
            if future is not None and not future.done(): future.set_result(result)
        except (ProtocolError, UnicodeError, ValueError, RecursionError) as error:
            # Unknown/malformed data cannot claim that a pending request succeeded.
            self.failed = True
            for future in self.pending.values():
                if not future.done(): future.set_exception(ProtocolError(str(error)))

    def disconnected(self):
        self.failed = True
        self.receiver.reset()
        for future in self.pending.values():
            if not future.done(): future.set_exception(ConnectionError("BLE disconnected; request outcome unknown"))

    async def request(self, kind: str, **fields) -> dict:
        async with self.lock:
            if self.failed: raise ConnectionError("session failed; reconnect and handshake, do not replay")
            if kind not in self.REQUEST_TYPES or any(k in fields for k in ("v", "type", "session", "att_payload")):
                raise ProtocolError("unsupported diagnostic request")
            if kind == "hello": fields = {**fields, "att_payload": self.offered_payload}
            mid = self.next_id
            if mid > 65535: raise ProtocolError("message IDs exhausted; create a new connection")
            self.next_id += 1
            payload = json.dumps({"v": 1, "type": kind, "session": self.session, **fields},
                                 separators=(",", ":"), ensure_ascii=False).encode("utf-8")
            future = asyncio.get_running_loop().create_future()
            self.pending[mid] = future
            try:
                # Hello is always small; later writes use both peers' MTU cap.
                async def exchange():
                    for fragment in encode_fragments(payload, mid, self.att_payload):
                        await self.client.write_gatt_char(RX_UUID, fragment, response=True)
                    return await future
                reply = await asyncio.wait_for(exchange(), timeout=self.request_timeout)
                expected = self.REQUEST_TYPES[kind]
                if reply.get("type") != expected: raise ProtocolError("unexpected reply type")
                if kind == "hello":
                    if reply.get("name") != DEVICE_NAME or reply.get("max_message") != 4096 or not reply.get("boot_id"):
                        raise ProtocolError("invalid hello capabilities")
                    self.boot_id = reply["boot_id"]
                    agreed = reply.get("att_payload", 20)
                    if type(agreed) is not int or not 20 <= agreed <= self.offered_payload:
                        raise ProtocolError("invalid negotiated ATT payload")
                    self.att_payload = agreed
                elif kind in ("ping", "diagnostic", "time_status") and reply.get("boot_id") != self.boot_id:
                    raise ProtocolError("device restarted; new handshake required")
                elif kind == "time_sync" and reply.get("accepted") is not True:
                    raise ProtocolError("time sync rejected")
                if kind == "diagnostic":
                    for key in ("internal_free_bytes", "internal_min_bytes", "host_stack_free_bytes", "tx_stack_free_bytes"):
                        if type(reply.get(key)) is not int or reply[key] < 0: raise ProtocolError("invalid resource diagnostic")
                    keys = ("wifi_online", "weather_configured", "weather_available", "weather_cached")
                    if any(k in reply for k in keys) and not all(type(reply.get(k)) is bool for k in keys):
                        raise ProtocolError("invalid coexistence diagnostic")
                self.validate_reply(kind, reply)
                if kind == "time_status":
                    valid, source, age, epoch = (reply.get(k) for k in ("valid", "source", "last_sync_age_ms", "epoch_ms"))
                    if type(valid) is not bool or source not in ("ble", "ntp", "waiting") or type(age) is not int or type(epoch) is not int:
                        raise ProtocolError("invalid time status")
                    if valid != (source != "waiting") or (valid and (age < 0 or not 1700000000000 <= epoch < 4102444800000)) or (not valid and age != -1):
                        raise ProtocolError("inconsistent time status")
                return reply
            except BaseException:
                self.failed = True
                self.receiver.reset()
                raise
            finally:
                self.pending.pop(mid, None)
                if not future.done(): future.cancel()
                elif not future.cancelled(): future.exception()
            # There is deliberately no automatic request replay after timeout.

    def validate_reply(self, kind: str, reply: dict):
        """Additional validation for an explicitly selected business session."""


async def wait_security_ready(client, timeout: float = 45):
    async def poll():
        while True:
            if not client.is_connected: raise ConnectionError("Disconnected before authentication")
            value = bytes(await client.read_gatt_char(READY_UUID, use_cached=False))
            if value == b"\x01": return
            if value != b"\x00": raise ProtocolError("Invalid security readiness")
            await asyncio.sleep(.2)
    await asyncio.wait_for(poll(), timeout=timeout)


@asynccontextmanager
async def connected_session(device, auth_timeout: float = 45, att_payload_cap: int = 512, session_factory=ProbeSession):
    from bleak import BleakClient
    session = None
    def disconnected(_client):
        if session is not None: session.disconnected()
    # Manual numerical comparison remains in Windows Settings/on the panel.
    async with BleakClient(device, pair=False, timeout=60,
                           winrt={"use_cached_services": False},
                           disconnected_callback=disconnected) as client:
        if any(client.services.get_characteristic(u) is None for u in (RX_UUID, TX_UUID, READY_UUID)):
            raise ProtocolError("Missing Luna RX/TX/security readiness; flash current B0 firmware")
        await wait_security_ready(client, auth_timeout)
        session = session_factory(client, att_payload_cap=att_payload_cap)
        notifying = False
        try:
            await client.start_notify(TX_UUID, session.notification)
            notifying = True
            yield session
        finally:
            session.disconnected()
            if notifying and client.is_connected: await client.stop_notify(TX_UUID)


async def run(args) -> int:
    if sys.platform == "win32" and sys.getwindowsversion().build < 22000:
        raise RuntimeError("B0 Bleak 3.0.2 requires Windows 11 build 22000 or newer")
    try:
        from bleak import BleakScanner
    except ImportError as error:
        raise RuntimeError("Install requirements-ble.txt in the separate .venv-ble environment first") from error
    devices = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    candidates = [(device, adv) for device, adv in devices.values() if is_luna(adv)]
    if args.address:
        candidates = [(device, adv) for device, adv in candidates if device.address.lower() == args.address.lower()]
    print(f"Luna service matches: {len(candidates)}. No serial ports opened.")
    if not candidates:
        print("No Luna BLE advertisement. Check Luna firmware, power and Windows Bluetooth.")
        return 2
    if not args.connect:
        print("Scan only. Pair manually in Windows Settings, then use --connect for B0 hello/ping/time_sync.")
        return 0
    if len(candidates) != 1:
        raise RuntimeError("More than one Luna found; select one with --address")
    # Bleak 3.0.2 WinRT pair() supports CONFIRM_ONLY and auto-accepts it.
    # Use Windows Settings for human-confirmed numeric comparison instead.
    print("Pair Luna in Windows Bluetooth Settings first; compare numbers and confirm on both screens.")
    async with connected_session(candidates[0][0], att_payload_cap=getattr(args, "att_payload_cap", 512)) as session:
        hello = await session.request("hello")
        print(f"Hello: name={hello['name']}, mode={hello.get('mode')}, MTU={session.client.mtu_size}, ATT value={session.att_payload} bytes")
        rtts = []
        for _ in range(args.pings):
            start = time.monotonic()
            padding = getattr(args, "payload_bytes", 0)
            await session.request("ping", **({"padding": "p" * padding} if padding else {}))
            rtts.append((time.monotonic() - start) * 1000)
        p95 = sorted(rtts)[max(0, int(len(rtts) * .95 + .999999) - 1)]
        print(f"Ping replies: {len(rtts)}/{args.pings}; RTT median={statistics.median(rtts):.1f} ms, p95={p95:.1f} ms, max={max(rtts):.1f} ms")
        if args.time_sync:
            offset = int(datetime.now().astimezone().utcoffset().total_seconds() // 60)
            result = await session.request("time_sync", epoch_ms=int(time.time() * 1000),
                                           timezone_offset_minutes=offset)
            print(f"BLE time accepted; device timezone policy={result.get('timezone_policy')}")
        if getattr(args, "diagnostics", False):
            result = await session.request("diagnostic")
            print("Resources (bytes): " + ", ".join(f"{k}={result[k]}" for k in
                  ("internal_free_bytes", "internal_min_bytes", "host_stack_free_bytes", "tx_stack_free_bytes")))
            if "wifi_online" in result:
                print("Coexistence: " + ", ".join(f"{k}={result[k]}" for k in
                      ("wifi_online", "weather_configured", "weather_available", "weather_cached")))
    print("Probe complete: intentionally disconnecting. Use luna_ble_link.py for a persistent connection.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--connect", action="store_true", help="Connect after manual pairing in Windows Settings (never auto-pairs)")
    parser.add_argument("--time-sync", action="store_true", help="Explicitly send UTC time after handshake")
    parser.add_argument("--address", help="Disambiguate multiple Luna devices, not an authentication token")
    parser.add_argument("--scan-seconds", type=float, default=5)
    parser.add_argument("--pings", type=int, default=10)
    parser.add_argument("--att-payload-cap", type=int, default=512, help="Force a smaller cap for fallback tests, e.g. 20")
    parser.add_argument("--payload-bytes", type=int, default=0, help="Synthetic ping padding, 0..3800; no media action")
    parser.add_argument("--diagnostics", action="store_true", help="Read authenticated internal RAM/task-stack watermarks")
    args = parser.parse_args()
    if not 1 <= args.pings <= 100 or not 1 <= args.scan_seconds <= 30:
        parser.error("pings must be 1..100 and scan-seconds 1..30")
    if not 20 <= args.att_payload_cap <= 512 or not 0 <= args.payload_bytes <= 3800:
        parser.error("ATT payload cap must be 20..512 and ping padding 0..3800")
    if (args.time_sync or args.diagnostics or args.payload_bytes) and not args.connect:
        parser.error("--time-sync/--diagnostics/--payload-bytes require --connect")
    try: return asyncio.run(run(args))
    except KeyboardInterrupt: return 130
    except Exception as error:
        print(f"Luna BLE diagnostic failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__": raise SystemExit(main())
