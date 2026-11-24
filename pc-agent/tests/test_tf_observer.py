"""The TF observer must remain bounded, redact unrelated logs, and release UART."""

import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "observe-tf-readonly.py"
SPEC = importlib.util.spec_from_file_location("tf_observer", SCRIPT)
OBSERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OBSERVER)


class FakePort:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.in_waiting = 4096
        self.closed = False

    def open(self):
        pass

    def read(self, count):
        return self.chunks.pop(0) if self.chunks else b""

    def close(self):
        self.closed = True


class ObserverTests(unittest.TestCase):
    def observe(self, chunks):
        port = FakePort(chunks)
        output = io.StringIO()
        with patch.object(OBSERVER.serial, "Serial", return_value=port), \
             patch.object(OBSERVER.time, "monotonic", side_effect=[0, 0.1, 0.2, 2]), \
             patch("sys.argv", ["observer", "--port", "TESTPORT", "--seconds", "1"]), \
             contextlib.redirect_stdout(output):
            result = OBSERVER.main()
        self.assertTrue(port.closed)
        self.assertFalse(port.dtr)
        self.assertFalse(port.rts)
        return result, output.getvalue()

    def test_fragmented_pass_and_redaction(self):
        result, text = self.observe([
            b"I: private Wi-Fi line\nI: TF_TEST PA",
            b"SS: repeated_reads=20/20 wifi_online=1 file_writes=0\n",
        ])
        self.assertEqual(result, 0)
        self.assertNotIn("private Wi-Fi", text)

    def test_missing_pass_is_not_success(self):
        result, _ = self.observe([b"TF_TEST begin\n", b""])
        self.assertEqual(result, 1)

    def test_failure_is_not_overridden_by_later_pass(self):
        result, _ = self.observe([b"TF_TEST mount failed\n", b"TF_TEST PASS:\n"])
        self.assertEqual(result, 1)

    def test_crash_is_not_success(self):
        result, _ = self.observe([b"Guru Meditation Error\n", b"TF_TEST PASS:\n"])
        self.assertEqual(result, 1)

    def test_open_error_still_releases_port(self):
        port = FakePort([])
        with patch.object(OBSERVER.serial, "Serial", return_value=port), \
             patch.object(port, "open", side_effect=OBSERVER.serial.SerialException("busy")), \
             patch("sys.argv", ["observer", "--port", "TESTPORT"]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(OBSERVER.serial.SerialException):
                OBSERVER.main()
        self.assertTrue(port.closed)

    def test_missing_port_rejected_before_serial_access(self):
        with patch.object(OBSERVER.serial, "Serial") as serial_factory, \
             patch("sys.argv", ["observer"]), \
             contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                OBSERVER.main()
        self.assertEqual(error.exception.code, 2)
        serial_factory.assert_not_called()


if __name__ == "__main__":
    unittest.main()
