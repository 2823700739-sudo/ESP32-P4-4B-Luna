from pathlib import Path
import ctypes
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_ble_protocol import DEVICE_NAME, HEADER, ProtocolError, Reassembler, encode_fragments, att_payload_for_mtu


class FrameTests(unittest.TestCase):
    def test_actual_mtu_caps_and_unknown_fallback(self):
        for mtu, expected in ((23, 20), (64, 61), (256, 253), (517, 512),
                              (None, 20), (True, 20), (22, 20), (518, 20), (256.0, 20)):
            self.assertEqual(att_payload_for_mtu(mtu), expected)
        self.assertEqual(att_payload_for_mtu(256, 20), 20)
        for cap in (19, 513, True, 20.0):
            with self.assertRaises(ProtocolError): att_payload_for_mtu(256, cap)

    def test_device_name_is_exactly_luna(self):
        self.assertEqual(DEVICE_NAME, "Luna")

    def test_default_mtu_23_and_full_4k(self):
        for limit in (20, 61, 244, 512):
            data = bytes(range(256)) * 16
            packets = encode_fragments(data, 65535, limit)
            self.assertTrue(all(len(p) <= limit for p in packets))
            receiver = Reassembler()
            for p in packets:
                result = receiver.feed(p, now=0)
            self.assertEqual(result.payload, data)
            self.assertEqual(result.message_id, 65535)

    def test_out_of_order_and_duplicates(self):
        packets = encode_fragments("Luna 月夜".encode() * 50, 3)
        random.Random(17).shuffle(packets)
        receiver = Reassembler()
        for p in packets[:-1]:
            self.assertIsNone(receiver.feed(p, 0))
            self.assertIsNone(receiver.feed(p, 0))
        self.assertEqual(receiver.feed(packets[-1], 0).payload, "Luna 月夜".encode() * 50)

    def test_invalid_encode(self):
        for data, mid, limit in ((b"", 1, 20), (b"x" * 4097, 1, 20),
                                 (b"x", 0, 20), (b"x", 65536, 20),
                                 (b"x", 1, 12), (b"x", 1, 513)):
            with self.assertRaises(ProtocolError):
                encode_fragments(data, mid, limit)

    def test_bad_headers_and_bounds(self):
        valid = bytearray(encode_fragments(b"hi", 1)[0])
        cases = [b"", bytes(HEADER.size), bytes(513)]
        for index, value in ((0, 0), (1, 2), (2, 0), (6, 0)):
            p = valid.copy(); p[index] = value; cases.append(bytes(p))
        cases.append(HEADER.pack(0x4C, 1, 1, 4095, 4096, 0) + b"too far")
        cases.append(HEADER.pack(0x4C, 1, 1, 0, 4097, 0) + b"x")
        for p in cases:
            with self.assertRaises(ProtocolError):
                Reassembler().feed(p, 0)

    def test_crc_reject_and_recovery(self):
        packet = bytearray(encode_fragments(b"hello", 1)[0]); packet[-1] ^= 1
        receiver = Reassembler()
        with self.assertRaises(ProtocolError): receiver.feed(packet, 0)
        self.assertEqual(receiver.feed(encode_fragments(b"hello", 2)[0], 1).payload, b"hello")

    def test_conflict_reject_and_reset(self):
        packets = encode_fragments(b"long enough message", 1)
        receiver = Reassembler(); receiver.feed(packets[0], 0)
        corrupt = bytearray(packets[0]); corrupt[-1] ^= 1
        with self.assertRaises(ProtocolError): receiver.feed(corrupt, 0)
        for p in packets: result = receiver.feed(p, 1)
        self.assertEqual(result.payload, b"long enough message")

    def test_busy_reject_preserves_message(self):
        receiver = Reassembler(); packets = encode_fragments(b"a" * 30, 1)
        receiver.feed(packets[0], 0)
        with self.assertRaises(ProtocolError): receiver.feed(encode_fragments(b"b", 2)[0], 1)
        for p in packets[1:]: result = receiver.feed(p, 2)
        self.assertEqual(result.payload, b"a" * 30)

    def test_duplicate_cannot_extend_absolute_timeout(self):
        receiver = Reassembler(); p = encode_fragments(b"a" * 30, 1)[0]
        receiver.feed(p, 0); receiver.feed(p, 29)
        result = receiver.feed(encode_fragments(b"new", 2)[0], 30)
        self.assertEqual(result.payload, b"new")

    def test_disconnect_reset_discards_partial_bytes(self):
        receiver = Reassembler(); receiver.feed(encode_fragments(b"a" * 30, 1)[0], 0)
        receiver.reset()
        for p in encode_fragments(b"new session", 2): result = receiver.feed(p, 0)
        self.assertEqual(result.payload, b"new session")


class CRx(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint16), ("total", ctypes.c_uint16),
                ("received", ctypes.c_uint16), ("crc", ctypes.c_uint32),
                ("started_ms", ctypes.c_uint32), ("data", ctypes.c_uint8 * 4097),
                ("seen", ctypes.c_uint8 * 512)]


@unittest.skipUnless(shutil.which("gcc"), "native C parity tests require existing gcc")
class CParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="luna-ble-codec-")
        source = Path(__file__).resolve().parents[2] / "firmware/luna-panel/main/luna_ble_frame.c"
        library = Path(cls.temp.name) / "luna_frame.dll"
        subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared",
                        "-O2", str(source), "-o", str(library)], check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.luna_ble_frame_feed.argtypes = [ctypes.POINTER(CRx), ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]
        cls.lib.luna_ble_frame_encode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                                ctypes.c_size_t, ctypes.c_uint16, ctypes.c_size_t, ctypes.c_size_t]
        cls.lib.luna_ble_frame_encode.restype = ctypes.c_size_t

    @classmethod
    def tearDownClass(cls):
        # Windows must unload the test DLL before TemporaryDirectory removes it.
        if sys.platform == "win32":
            ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
            ctypes.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()

    def feed(self, rx, p, now=0):
        return self.lib.luna_ble_frame_feed(ctypes.byref(rx), p, len(p), now)

    def test_python_and_c_wire_bytes_match(self):
        for length in (1, 8, 9, 64, 2048, 4096):
            data = bytes(i % 251 for i in range(length))
            for limit in (20, 244, 512):
                for packet in encode_fragments(data, 17, limit):
                    offset = struct.unpack_from("<H", packet, 4)[0]
                    output = ctypes.create_string_buffer(512)
                    size = self.lib.luna_ble_frame_encode(output, 512, data, len(data), 17, offset, limit)
                    self.assertEqual(output.raw[:size], packet)

    def test_c_receives_shuffled_full_message_and_duplicates(self):
        data = bytes(range(256)) * 16
        packets = encode_fragments(data, 19)
        random.Random(91).shuffle(packets)
        rx = CRx()
        for p in packets[:-1]:
            self.assertEqual(self.feed(rx, p), 0)
            self.assertEqual(self.feed(rx, p), 0)
        self.assertEqual(self.feed(rx, packets[-1]), 1)
        self.assertEqual(bytes(rx.data[:4096]), data)

    def test_c_timeout_including_clock_wrap(self):
        rx = CRx(); packets = encode_fragments(b"a" * 30, 1)
        self.feed(rx, packets[0], 0xFFFFF000)
        self.assertEqual(self.feed(rx, encode_fragments(b"new", 2)[0], 26000), 1)

    def test_c_conflict_and_crc_reject(self):
        rx = CRx(); packets = encode_fragments(b"a" * 30, 1)
        self.feed(rx, packets[0]); bad = bytearray(packets[0]); bad[-1] ^= 1
        self.assertEqual(self.feed(rx, bytes(bad)), -1)
        bad = bytearray(encode_fragments(b"hello", 2)[0]); bad[-1] ^= 1
        self.assertEqual(self.feed(rx, bytes(bad)), -1)

    def test_random_bad_packets_have_same_reject_behavior(self):
        rng = random.Random(30)
        for _ in range(500):
            p = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 90)))
            self.assertEqual(self.feed(CRx(), p), -1)
            with self.assertRaises(ProtocolError): Reassembler().feed(p, 0)

    def test_c_rejects_valid_magic_out_of_bounds_headers(self):
        for packet in (HEADER.pack(0x4C, 1, 0, 0, 1, 0) + b'x',
                       HEADER.pack(0x4C, 1, 1, 0, 4097, 0) + b'x',
                       HEADER.pack(0x4C, 1, 1, 4095, 4096, 0) + b'xx',
                       HEADER.pack(0x4C, 1, 1, 65535, 5, 0) + b'x'):
            self.assertEqual(self.feed(CRx(), packet), -1)

    def test_c_busy_reject_does_not_lose_current_assembly(self):
        rx = CRx(); packets = encode_fragments(b'a' * 30, 1)
        self.assertEqual(self.feed(rx, packets[0]), 0)
        self.assertEqual(self.feed(rx, encode_fragments(b'b', 2)[0]), -1)
        for p in packets[1:]: result = self.feed(rx, p)
        self.assertEqual(result, 1)
        self.assertEqual(bytes(rx.data[:30]), b'a' * 30)


if __name__ == "__main__": unittest.main()
