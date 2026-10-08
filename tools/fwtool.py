import argparse
import struct
import sys
import time
import zlib

import serial

SOF = 0xA5
MAX_PAYLOAD = 256
CHUNK = 252

MSG_PING = 0x01
MSG_START = 0x10
MSG_DATA = 0x11
MSG_END = 0x12
MSG_ACK = 0x81
MSG_NACK = 0x82


def encode(msg_type, payload=b""):
    body = struct.pack("<BH", msg_type, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<I", zlib.crc32(body))


class Link:
    def __init__(self, port_name):
        self.port = serial.Serial(port_name, 115200, timeout=0.05)
        self.buf = bytearray()

    def send(self, msg_type, payload=b""):
        self.port.write(encode(msg_type, payload))

    def drain(self):
        time.sleep(0.1)
        self.port.reset_input_buffer()
        self.buf.clear()

    def read_frame(self, timeout):
        """Returns (type, payload), or None if no valid frame arrives in time."""
        deadline = time.monotonic() + timeout
        while True:
            frame = self._extract()
            if frame is not None:
                return frame
            if time.monotonic() >= deadline:
                return None
            self.buf += self.port.read(64)

    def _extract(self):
        while True:
            start = self.buf.find(bytes([SOF]))
            if start < 0:
                self.buf.clear()
                return None
            del self.buf[:start]
            if len(self.buf) < 4:
                return None
            length = self.buf[2] | (self.buf[3] << 8)
            if length > MAX_PAYLOAD:
                del self.buf[:1]
                continue
            total = 8 + length
            if len(self.buf) < total:
                return None
            body = bytes(self.buf[1:4 + length])
            (crc,) = struct.unpack_from("<I", self.buf, 4 + length)
            if zlib.crc32(body) == crc:
                frame = (self.buf[1], bytes(self.buf[4:4 + length]))
                del self.buf[:total]
                return frame
            del self.buf[:1]


def connect(link):
    """Pings until the bootloader answers. Returns the protocol version."""
    print("sending PING - press the black reset button on the board now", flush=True)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        link.send(MSG_PING)
        frame = link.read_frame(0.2)
        if frame is None:
            continue
        msg_type, payload = frame
        if msg_type == MSG_ACK and len(payload) == 1:
            link.drain()
            return payload[0]
    sys.exit("no answer from the bootloader within 15 s")


def request(link, msg_type, payload, timeout, retries=3):
    """Sends a message and returns the 32-bit value from its ACK."""
    for _ in range(retries):
        link.send(msg_type, payload)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = link.read_frame(deadline - time.monotonic())
            if frame is None:
                break
            reply_type, reply = frame
            if reply_type == MSG_ACK and len(reply) == 4:
                return struct.unpack("<I", reply)[0]
            if reply_type == MSG_NACK and len(reply) == 1:
                sys.exit("device refused message 0x%02x: error 0x%02x" % (msg_type, reply[0]))
    sys.exit("no reply to message 0x%02x after %d attempts" % (msg_type, retries))


def cmd_ping(link, args):
    print("bootloader answered, protocol version", connect(link))
    return 0


def cmd_send(link, args):
    if args.image is None:
        sys.exit("usage: fwtool.py PORT send IMAGE.bin")
    with open(args.image, "rb") as f:
        image = f.read()
    crc = zlib.crc32(image)
    print("image: %d bytes, CRC-32 0x%08x" % (len(image), crc))

    connect(link)
    started = time.monotonic()

    request(link, MSG_START, struct.pack("<II", len(image), crc), timeout=10)
    print("slot B erased")

    offset = 0
    while offset < len(image):
        chunk = image[offset:offset + CHUNK]
        next_offset = request(link, MSG_DATA, struct.pack("<I", offset) + chunk, timeout=1)
        if next_offset != offset + len(chunk):
            sys.exit("device is at offset %d, expected %d" % (next_offset, offset + len(chunk)))
        offset = next_offset
        print("\rsent %d / %d bytes" % (offset, len(image)), end="", flush=True)
    print()

    request(link, MSG_END, b"", timeout=5)
    print("image accepted: CRC matches in flash")
    print("transfer took %.2f s" % (time.monotonic() - started))
    return 0


def main():
    parser = argparse.ArgumentParser(description="STM32 bootloader host tool")
    parser.add_argument("port", help="serial port, e.g. COM24")
    parser.add_argument("command", choices=["ping", "send"])
    parser.add_argument("image", nargs="?", help="binary image for send")
    args = parser.parse_args()
    link = Link(args.port)
    return {"ping": cmd_ping, "send": cmd_send}[args.command](link, args)


if __name__ == "__main__":
    sys.exit(main())
