from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luna_link_protocol import FrameDecoder, MessageType, encode_frame  # noqa: E402


class FrameDecoderTests(unittest.TestCase):
    def test_fragmented_frame(self) -> None:
        encoded = encode_frame(MessageType.PING, 42, b"hello")
        decoder = FrameDecoder()

        self.assertEqual(decoder.feed(encoded[:7]), [])
        self.assertEqual(decoder.feed(encoded[7:-2]), [])
        frames = decoder.feed(encoded[-2:])

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].message_type, MessageType.PING)
        self.assertEqual(frames[0].request_id, 42)
        self.assertEqual(frames[0].payload, b"hello")

    def test_coalesced_frames(self) -> None:
        decoder = FrameDecoder()
        frames = decoder.feed(
            encode_frame(MessageType.PING, 1, b"one")
            + encode_frame(MessageType.PONG, 2, b"two")
        )

        self.assertEqual(
            [(frame.request_id, frame.payload) for frame in frames],
            [(1, b"one"), (2, b"two")],
        )

    def test_bad_crc_resynchronizes_to_next_frame(self) -> None:
        damaged = bytearray(encode_frame(MessageType.PING, 1, b"bad"))
        damaged[-1] ^= 0x80
        valid = encode_frame(MessageType.PONG, 2, b"good")

        frames = FrameDecoder().feed(bytes(damaged) + valid)

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].request_id, 2)
        self.assertEqual(frames[0].payload, b"good")

    def test_oversized_header_resynchronizes_to_next_frame(self) -> None:
        invalid_header = struct.pack("<4sBBBBII", b"LUNA", 1, 3, 0, 0, 99, 4097)
        valid = encode_frame(MessageType.PONG, 7)

        frames = FrameDecoder().feed(invalid_header + valid)

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].request_id, 7)

    def test_partial_magic_survives_between_reads(self) -> None:
        decoder = FrameDecoder()
        self.assertEqual(decoder.feed(b"noiseLU"), [])
        frames = decoder.feed(encode_frame(MessageType.PING, 9)[2:])
        self.assertEqual(frames[0].request_id, 9)

    def test_large_state_snapshot_survives_many_chunks(self) -> None:
        payload = b"x" * 3500
        encoded = encode_frame(MessageType.STATE_SNAPSHOT, 31, payload)
        decoder = FrameDecoder()
        frames = []
        for offset in range(0, len(encoded), 127):
            frames.extend(decoder.feed(encoded[offset : offset + 127]))
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].payload, payload)


if __name__ == "__main__":
    unittest.main()
