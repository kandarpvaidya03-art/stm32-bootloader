import argparse
import struct
import sys
import time
import zlib

import serial

SOF = 0xA5
MAX_PAYLOAD = 256
MSG_PING = 0x01
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


def cmd_ping(link):
    print("sending PING - press the black reset button on the board now", flush=True)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        link.send(MSG_PING)
        frame = link.read_frame(0.2)
        if frame is None:
            continue
        msg_type, payload = frame
        if msg_type == MSG_ACK and len(payload) == 1:
            print("bootloader answered, protocol version", payload[0])
            return 0
        print("unexpected reply: type 0x%02x payload %s" % (msg_type, payload.hex()))
        return 1
    print("no answer from the bootloader within 15 s")
    return 1


def main():
    parser = argparse.ArgumentParser(description="STM32 bootloader host tool")
    parser.add_argument("port", help="serial port, e.g. COM24")
    parser.add_argument("command", choices=["ping"])
    args = parser.parse_args()
    return cmd_ping(Link(args.port))


if __name__ == "__main__":
    sys.exit(main())
