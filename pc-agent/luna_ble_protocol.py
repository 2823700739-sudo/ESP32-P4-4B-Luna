"""Luna BLE v1 framing, independent of Bleak/COM ports and business actions."""
from __future__ import annotations

from dataclasses import dataclass
import struct
import time
import zlib

DEVICE_NAME = "Luna"
SERVICE_UUID = "c4a10001-9e7a-4b61-bb2b-15d21b8a4c01"
RX_UUID = "c4a10002-9e7a-4b61-bb2b-15d21b8a4c01"
TX_UUID = "c4a10003-9e7a-4b61-bb2b-15d21b8a4c01"
READY_UUID = "c4a10004-9e7a-4b61-bb2b-15d21b8a4c01"
HEADER = struct.Struct("<BBHHHI")
MAX_MESSAGE = 4096
TIMEOUT_SECONDS = 30.0


def att_payload_for_mtu(mtu: int | None, cap: int = 512) -> int:
    """ATT write/notify value is MTU minus 3, additionally bounded by framing."""
    if type(cap) is not int or not 20 <= cap <= 512:
        raise ProtocolError("ATT payload cap must be an integer from 20 to 512")
    if type(mtu) is not int or not 23 <= mtu <= 517:
        return 20
    return min(mtu - 3, cap, 512)


class ProtocolError(ValueError):
    pass


def encode_fragments(payload: bytes, message_id: int, att_payload: int = 20) -> list[bytes]:
    if not 1 <= message_id <= 65535:
        raise ProtocolError("message_id must be 1..65535")
    if not 1 <= len(payload) <= MAX_MESSAGE:
        raise ProtocolError("logical message must be 1..4096 bytes")
    if not HEADER.size < att_payload <= 512:
        raise ProtocolError("invalid ATT payload limit")
    size = att_payload - HEADER.size
    crc = zlib.crc32(payload)
    return [HEADER.pack(0x4C, 1, message_id, offset, len(payload), crc) + payload[offset:offset + size]
            for offset in range(0, len(payload), size)]


@dataclass(frozen=True)
class Message:
    message_id: int
    payload: bytes


class Reassembler:
    """One bounded message per direction; caller serializes logical sends.

    Out-of-order and exact duplicate fragments are accepted; conflicting overlap
    is rejected. reset() is mandatory on disconnect/session change. Completed
    message replay is a business/session concern, not a guarantee of this codec.
    """

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self._key: tuple[int, int, int] | None = None
        self._started = 0.0
        self._data = bytearray()
        self._seen = bytearray()
        self._received = 0

    def feed(self, packet: bytes, now: float | None = None) -> Message | None:
        now = time.monotonic() if now is None else now
        if self._key is not None and now - self._started >= TIMEOUT_SECONDS:
            self.reset()
        if not HEADER.size < len(packet) <= 512:
            raise ProtocolError("invalid fragment size")
        magic, version, mid, offset, total, crc = HEADER.unpack_from(packet)
        chunk = packet[HEADER.size:]
        if magic != 0x4C or version != 1 or mid == 0:
            raise ProtocolError("unsupported header")
        if not 1 <= total <= MAX_MESSAGE or offset + len(chunk) > total:
            raise ProtocolError("fragment outside message bounds")
        key = (mid, total, crc)
        if self._key is not None and self._key != key:
            raise ProtocolError("another logical message is in progress")
        if self._key is None:
            self._key, self._started = key, now
            self._data, self._seen = bytearray(total), bytearray(total)
        # Validate all overlap before mutating any byte.
        for index, value in enumerate(chunk, offset):
            if self._seen[index] and self._data[index] != value:
                self.reset()
                raise ProtocolError("conflicting duplicate fragment")
        for index, value in enumerate(chunk, offset):
            if not self._seen[index]:
                self._data[index], self._seen[index] = value, 1
                self._received += 1
        if self._received != total:
            return None
        data = bytes(self._data)
        self.reset()
        if zlib.crc32(data) != crc:
            raise ProtocolError("message CRC mismatch")
        return Message(mid, data)
