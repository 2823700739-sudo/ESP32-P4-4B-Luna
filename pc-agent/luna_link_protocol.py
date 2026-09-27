"""Luna Link P0 binary framing shared by the Windows USB probe."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
import zlib


MAGIC = b"LUNA"
VERSION = 1
MAX_PAYLOAD = 512
HEADER = struct.Struct("<4sBBBBII")
CRC = struct.Struct("<I")


class MessageType(IntEnum):
    HELLO = 1
    HELLO_ACK = 2
    PING = 3
    PONG = 4
    TOUCH_TEST = 5


@dataclass(frozen=True, slots=True)
class Frame:
    message_type: int
    request_id: int
    payload: bytes
    flags: int = 0


def encode_frame(
    message_type: int | MessageType,
    request_id: int,
    payload: bytes = b"",
    *,
    flags: int = 0,
) -> bytes:
    if not isinstance(payload, bytes):
        raise TypeError("payload must be bytes")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload exceeds {MAX_PAYLOAD} bytes")
    if not 0 <= request_id <= 0xFFFFFFFF:
        raise ValueError("request_id must fit in uint32")
    if not 0 <= int(message_type) <= 0xFF or not 0 <= flags <= 0xFF:
        raise ValueError("message_type and flags must fit in uint8")

    content = HEADER.pack(
        MAGIC,
        VERSION,
        int(message_type),
        flags,
        0,
        request_id,
        len(payload),
    ) + payload
    return content + CRC.pack(zlib.crc32(content) & 0xFFFFFFFF)


class FrameDecoder:
    """Incrementally decode fragmented or coalesced Luna Link frames."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def reset(self) -> None:
        self._buffer.clear()

    def feed(self, data: bytes) -> list[Frame]:
        if not isinstance(data, bytes):
            raise TypeError("data must be bytes")
        self._buffer.extend(data)
        frames: list[Frame] = []

        while True:
            magic_index = self._buffer.find(MAGIC)
            if magic_index < 0:
                # Keep only a possible partial magic prefix across reads.
                del self._buffer[:-3]
                return frames
            if magic_index:
                del self._buffer[:magic_index]
            if len(self._buffer) < HEADER.size:
                return frames

            magic, version, message_type, flags, _reserved, request_id, payload_length = (
                HEADER.unpack_from(self._buffer)
            )
            if magic != MAGIC or version != VERSION or payload_length > MAX_PAYLOAD:
                del self._buffer[0]
                continue

            frame_length = HEADER.size + payload_length + CRC.size
            if len(self._buffer) < frame_length:
                return frames

            content = self._buffer[: HEADER.size + payload_length]
            expected_crc = CRC.unpack_from(self._buffer, HEADER.size + payload_length)[0]
            if zlib.crc32(content) & 0xFFFFFFFF != expected_crc:
                del self._buffer[0]
                continue

            payload = bytes(self._buffer[HEADER.size : HEADER.size + payload_length])
            frames.append(Frame(message_type, request_id, payload, flags))
            del self._buffer[:frame_length]

