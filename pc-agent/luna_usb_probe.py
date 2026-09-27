"""Discover Luna's native USB CDC port and exercise Luna Link P0."""

from __future__ import annotations

import argparse
import json
import secrets
import time
from typing import Iterator

import serial
from serial.tools import list_ports

from luna_link_protocol import Frame, FrameDecoder, MessageType, encode_frame


ESPRESSIF_VID = 0x303A
LUNA_P0_PID = 0x4001


def candidate_ports(explicit_port: str | None = None) -> Iterator[str]:
    if explicit_port:
        yield explicit_port
        return
    for port in list_ports.comports():
        if port.vid == ESPRESSIF_VID and port.pid == LUNA_P0_PID:
            yield port.device


def next_request_id() -> int:
    return secrets.randbelow(0xFFFFFFFF) + 1


def read_frames(port: serial.Serial, decoder: FrameDecoder, deadline: float) -> Iterator[Frame]:
    while time.monotonic() < deadline:
        data = port.read(port.in_waiting or 1)
        if data:
            yield from decoder.feed(data)


def handshake(port: serial.Serial, timeout: float = 2.0) -> tuple[FrameDecoder, dict]:
    decoder = FrameDecoder()
    request_id = next_request_id()
    payload = json.dumps({"client": "luna-usb-probe", "protocol": 1}).encode("utf-8")
    port.reset_input_buffer()
    port.write(encode_frame(MessageType.HELLO, request_id, payload))
    port.flush()

    for frame in read_frames(port, decoder, time.monotonic() + timeout):
        if frame.message_type != MessageType.HELLO_ACK or frame.request_id != request_id:
            continue
        identity = json.loads(frame.payload.decode("utf-8"))
        if identity.get("device") != "luna" or identity.get("protocol") != 1:
            raise RuntimeError("device answered but is not Luna Link protocol 1")
        return decoder, identity
    raise TimeoutError("Luna Link handshake timed out")


def open_luna(explicit_port: str | None = None) -> tuple[serial.Serial, FrameDecoder, dict]:
    errors: list[str] = []
    for device in candidate_ports(explicit_port):
        port: serial.Serial | None = None
        try:
            port = serial.Serial(device, 115200, timeout=0.1, write_timeout=1.0)
            port.dtr = True
            time.sleep(0.15)
            decoder, identity = handshake(port)
            return port, decoder, identity
        except (OSError, serial.SerialException, TimeoutError, RuntimeError, ValueError) as error:
            errors.append(f"{device}: {error}")
            if port is not None:
                try:
                    port.close()
                except (OSError, serial.SerialException):
                    pass
    if errors:
        raise ConnectionError("; ".join(errors))
    raise ConnectionError("no USB device with VID 303A and PID 4001 was found")


def run_session(port: serial.Serial, decoder: FrameDecoder, *, once: bool) -> None:
    outstanding: dict[int, int] = {}
    next_ping_at = 0.0
    print("Luna Link is ready. Touch events and ping latency will appear below.")

    while True:
        now = time.monotonic()
        if now >= next_ping_at:
            request_id = next_request_id()
            payload = str(time.time_ns()).encode("ascii")
            outstanding[request_id] = time.perf_counter_ns()
            port.write(encode_frame(MessageType.PING, request_id, payload))
            port.flush()
            next_ping_at = now + 2.0

        for frame in read_frames(port, decoder, min(next_ping_at, time.monotonic() + 0.25)):
            if frame.message_type == MessageType.PONG and frame.request_id in outstanding:
                latency_ms = (time.perf_counter_ns() - outstanding.pop(frame.request_id)) / 1_000_000
                print(f"PONG request={frame.request_id} latency={latency_ms:.3f} ms")
                if once:
                    return
            elif frame.message_type == MessageType.TOUCH_TEST:
                print(f"TOUCH {frame.payload.decode('utf-8', errors='replace')}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="probe only this COM port instead of auto-discovery")
    parser.add_argument("--once", action="store_true", help="exit after the first successful PONG")
    parser.add_argument("--retry-seconds", type=float, default=1.0)
    args = parser.parse_args()

    try:
        while True:
            try:
                port, decoder, identity = open_luna(args.port)
                print(
                    f"Connected to Luna on {port.port}: "
                    f"firmware={identity.get('firmware', 'unknown')} "
                    f"capabilities={identity.get('capabilities', [])}"
                )
                try:
                    run_session(port, decoder, once=args.once)
                finally:
                    port.close()
                if args.once:
                    return 0
            except (ConnectionError, OSError, serial.SerialException) as error:
                print(f"Waiting for Luna USB: {error}")
            time.sleep(max(args.retry_seconds, 0.1))
    except KeyboardInterrupt:
        print("\nLuna USB probe stopped.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
