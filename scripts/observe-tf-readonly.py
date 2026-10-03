"""Bounded UART observation for the explicitly enabled TF read-only firmware.

Does not send commands, reset the board, or open the native OTG port.
Only prints TF diagnostic lines and crash markers, not credentials or filenames.
"""

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Current UART port detected on this machine")
    parser.add_argument("--seconds", type=float, default=60)
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    passed = False
    failed = False
    try:
        port.open()
        print(f"Observing {args.port}; reset=disabled; closes automatically.", flush=True)
        deadline = time.monotonic() + max(1, min(args.seconds, 180))
        pending = bytearray()
        while time.monotonic() < deadline:
            data = port.read(min(port.in_waiting or 1, 4096))
            if not data:
                continue
            pending.extend(data)
            while b"\n" in pending:
                raw, _, remainder = pending.partition(b"\n")
                pending = bytearray(remainder)
                line = raw.decode("utf-8", errors="replace").strip()
                if "TF_TEST" in line or "TF card test failed" in line:
                    print(line, flush=True)
                    passed |= "TF_TEST PASS:" in line
                    failed |= "failed" in line.lower() or "not ready" in line.lower()
                elif any(marker in line for marker in ("Guru Meditation", "assert failed", "panic'ed", "Backtrace:")):
                    print(line, flush=True)
                    failed = True
            if len(pending) > 16384:
                pending.clear()
        print(f"Result: observed_pass={passed}, observed_failure={failed}", flush=True)
        return 0 if passed and not failed else 1
    finally:
        port.close()
        print(f"Released {args.port}.", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
