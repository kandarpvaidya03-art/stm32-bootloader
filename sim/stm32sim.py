"""Python simulator of the STM32F401RE bootloader.

It models the parts of the device the bootloader depends on, so the host tools
and the update and rollback logic can be exercised without a board:

- NOR flash with the F401RE's uneven sector map. Erase sets a sector to 0xFF;
  programming can only clear bits, exactly like the real part.
- The boot state flags, the install/backup/rollback steps and the UART update
  protocol, written to follow boot/main.c, boot/install.c and boot/update.c.
- Fault injection: power can be cut at any flash operation, and frames can be
  corrupted on the link.

The simulator does not execute ARM code. When the bootloader would jump to the
application, the simulator checks the vector table the same way the bootloader
does and then models the sample application: it prints its version and sets the
"confirmed" flag, unless the image contains the string the non-confirming build
prints. That is enough to test update and rollback end to end.

Use it from Python (see sim/tests) or as a TCP server that fwtool.py connects to:

    python sim/stm32sim.py --pubkey-from keys/signing_key.pem --port 5555
    python tools/fwtool.py socket://localhost:5555 send build/app-v2-signed.bin
"""

import argparse
import hashlib
import random
import re
import socket
import struct
import time
import zlib

# ---------------------------------------------------------------- memory map

FLASH_BASE = 0x08000000
FLASH_SIZE = 512 * 1024
SECTORS = [  # (start offset, size) for sectors 0..7, as on the F401RE
    (0x00000, 16 * 1024), (0x04000, 16 * 1024), (0x08000, 16 * 1024),
    (0x0C000, 16 * 1024), (0x10000, 64 * 1024), (0x20000, 128 * 1024),
    (0x40000, 128 * 1024), (0x60000, 128 * 1024),
]

STATE_BASE, STATE_SECTOR = 0x0800C000, 3
SLOT_A_BASE, SLOT_A_SECTOR = 0x08020000, 5
SLOT_B_BASE, SLOT_B_SECTOR = 0x08040000, 6
BACKUP_BASE, BACKUP_SECTOR = 0x08060000, 7
SLOT_SIZE = 0x20000
RAM_START, RAM_END = 0x20000000, 0x20018000
WATCHDOG_TIMEOUT_S = 4.0  # IWDG: LSI / 64, reload 2000 (common/iwdg.c)

# ---------------------------------------------------------------- image format

IMAGE_MAGIC = 0x31495746  # "FWI1"
HEADER_SIZE = 512
SIGNED_SIZE = 48
OFF_VERSION, OFF_SIZE, OFF_HASH, OFF_SIG = 4, 8, 16, 48
NO_CONFIRM_MARKER = b"this build never confirms itself"

# ---------------------------------------------------------------- boot state

REQUESTED, BACKUP_DONE, INSTALL_DONE, TRIAL_STARTED, CONFIRMED, ROLLBACK_DONE = range(6)
FLAG_NAMES = ["requested", "backup_done", "install_done", "trial_started",
              "confirmed", "rollback_done"]
FLAG_SET = 0x5AFE5AFE

RUN_APP, BACKUP, INSTALL, START_TRIAL, ROLLBACK, CLEAR_STATE = range(6)


def boot_next_action(f):
    """Same decision as common/bootplan.c. f is a dict of flag name -> bool."""
    if not f["requested"]:
        return RUN_APP
    if f["confirmed"] or f["rollback_done"]:
        return CLEAR_STATE
    if not f["backup_done"]:
        return BACKUP
    if not f["install_done"]:
        return INSTALL
    if not f["trial_started"]:
        return START_TRIAL
    return ROLLBACK

# ---------------------------------------------------------------- protocol

SOF = 0xA5
MAX_PAYLOAD = 256
MSG_PING, MSG_START, MSG_DATA, MSG_END = 0x01, 0x10, 0x11, 0x12
MSG_ACK, MSG_NACK = 0x81, 0x82
PROTOCOL_VERSION = 1

ERR_FRAME_LENGTH = 0x02
ERR_FRAME_CRC = 0x03
ERR_UNKNOWN_TYPE = 0x10
ERR_BAD_LENGTH = 0x20
ERR_TOO_BIG = 0x21
ERR_FLASH = 0x22
ERR_STATE = 0x23
ERR_OFFSET = 0x24
ERR_IMAGE_CRC = 0x25
ERR_IMAGE_INVALID = 0x26
ERR_SIGNATURE = 0x27
ERR_BUSY = 0x28


def encode_frame(msg_type, payload=b""):
    body = struct.pack("<BH", msg_type, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<I", zlib.crc32(body))


class PowerLoss(Exception):
    """Raised inside the device when injected power loss happens."""


class FrameParser:
    """Byte-wise parser with the same states and results as common/frame.c."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.state = "sof"
        self.type = 0
        self.length = 0
        self.payload = bytearray()
        self.crc = bytearray()

    def feed(self, byte):
        """Returns None (incomplete), "ready", or an error code."""
        if self.state == "sof":
            if byte == SOF:
                self.state = "type"
        elif self.state == "type":
            self.type = byte
            self.state = "len_lo"
        elif self.state == "len_lo":
            self.length = byte
            self.state = "len_hi"
        elif self.state == "len_hi":
            self.length |= byte << 8
            if self.length > MAX_PAYLOAD:
                self.state = "sof"
                return ERR_FRAME_LENGTH
            self.payload = bytearray()
            self.crc = bytearray()
            self.state = "payload" if self.length else "crc"
        elif self.state == "payload":
            self.payload.append(byte)
            if len(self.payload) == self.length:
                self.state = "crc"
        elif self.state == "crc":
            self.crc.append(byte)
            if len(self.crc) == 4:
                self.state = "sof"
                body = struct.pack("<BH", self.type, self.length) + bytes(self.payload)
                if zlib.crc32(body) == struct.unpack("<I", bytes(self.crc))[0]:
                    return "ready"
                return ERR_FRAME_CRC
        return None

# ---------------------------------------------------------------- flash


class Flash:
    """NOR flash: erase sets 0xFF per sector, programming can only clear bits."""

    def __init__(self, seed=0):
        self.mem = bytearray(b"\xff" * FLASH_SIZE)
        self.ops = 0               # flash operations performed so far
        self.cut_power_at = None   # operation index at which power is lost
        self.erases = [0] * len(SECTORS)
        self.rng = random.Random(seed)

    def _maybe_lose_power(self, partial):
        if self.cut_power_at is not None and self.ops == self.cut_power_at:
            self.cut_power_at = None
            partial()
            raise PowerLoss("power lost at flash operation %d" % self.ops)
        self.ops += 1

    def read(self, addr, n):
        off = addr - FLASH_BASE
        return bytes(self.mem[off:off + n])

    def read32(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def erase_sector(self, sector):
        start, size = SECTORS[sector]

        def interrupted():
            # ST does not define the contents of a sector whose erase was cut
            # short. Model it as random data.
            self.mem[start:start + size] = self.rng.randbytes(size)

        self._maybe_lose_power(interrupted)
        self.mem[start:start + size] = b"\xff" * size
        self.erases[sector] += 1

    def write_word(self, addr, value):
        if addr % 4:
            raise ValueError("unaligned flash write at 0x%08x" % addr)
        off = addr - FLASH_BASE
        old = struct.unpack_from("<I", self.mem, off)[0]

        def interrupted():
            # A word program cut short can leave only some of the bits cleared.
            partial = value | self.rng.getrandbits(32)
            struct.pack_into("<I", self.mem, off, old & partial)

        self._maybe_lose_power(interrupted)
        struct.pack_into("<I", self.mem, off, old & value)
        return 0 if (old & value) == value else -1

# ---------------------------------------------------------------- device


class Device:
    """The NUCLEO-F401RE running the bootloader, modelled step by step.

    Call reset() to model pressing the reset button or a power cycle,
    receive(data) to deliver UART bytes, and line_quiet() when the host has been
    silent for the bootloader's 500 ms listen window. Output is collected in
    self.uart_out.
    """

    def __init__(self, public_key_xy, seed=0):
        self.flash = Flash(seed)
        self.public_key = public_key_xy  # 64 bytes: X then Y, big-endian
        self.uart_out = bytearray()
        self.parser = FrameParser()
        self.mode = "off"   # "bootloader", "app", "waiting" (no valid image) or "off"
        self.running_version = None
        self.up = None      # transfer in progress: dict(size, crc, offset)
        self.boot_count = 0
        self.watchdog_running = False
        self.app_kicks_watchdog = False
        self.last_reset_watchdog = False

    # -------------------------------------------------- helpers

    def log(self, text):
        self.uart_out += (text + "\r\n").encode()

    def send(self, msg_type, payload=b""):
        self.uart_out += encode_frame(msg_type, payload)

    def ack32(self, value):
        self.send(MSG_ACK, struct.pack("<I", value))

    def nack(self, code):
        self.send(MSG_NACK, bytes([code]))

    def flags(self):
        return {name: self.flash.read32(STATE_BASE + 4 * i) == FLAG_SET
                for i, name in enumerate(FLAG_NAMES)}

    def set_flag(self, index):
        addr = STATE_BASE + 4 * index
        if self.flash.read32(addr) == FLAG_SET:
            return 0
        self.flash.write_word(addr, FLAG_SET)
        return 0 if self.flash.read32(addr) == FLAG_SET else -1

    def clear_flags(self):
        if all(self.flash.read32(STATE_BASE + 4 * i) == 0xFFFFFFFF for i in range(6)):
            return 0
        self.flash.erase_sector(STATE_SECTOR)
        return 0

    # -------------------------------------------------- image checks

    def image_ok(self, base, check_signature=True):
        """image_verify() plus image_signature_ok(), as in boot/install.c."""
        header = self.flash.read(base, HEADER_SIZE)
        magic, version, size = struct.unpack_from("<III", header, 0)
        if magic != IMAGE_MAGIC:
            return False
        if size == 0 or size > SLOT_SIZE - HEADER_SIZE:
            return False
        body = self.flash.read(base + HEADER_SIZE, size)
        if hashlib.sha256(body).digest() != header[OFF_HASH:OFF_HASH + 32]:
            return False
        if not check_signature:
            return True
        return verify_signature(self.public_key, header[:SIGNED_SIZE],
                                header[OFF_SIG:OFF_SIG + 64])

    def image_status(self, base):
        """The error code the update handler reports for a bad image, or 0."""
        header = self.flash.read(base, HEADER_SIZE)
        if not self.image_ok(base, check_signature=False):
            return ERR_IMAGE_INVALID
        if not verify_signature(self.public_key, header[:SIGNED_SIZE],
                                header[OFF_SIG:OFF_SIG + 64]):
            return ERR_SIGNATURE
        return 0

    def vectors_look_valid(self):
        vectors = SLOT_A_BASE + HEADER_SIZE
        sp = self.flash.read32(vectors)
        pc = self.flash.read32(vectors + 4)
        if sp <= RAM_START or sp > RAM_END:
            return False
        if pc < vectors or pc >= SLOT_A_BASE + SLOT_SIZE:
            return False
        return (pc & 1) == 1

    # -------------------------------------------------- install steps

    def copy_slot(self, dst_base, dst_sector, src_base):
        size = self.flash.read32(src_base + OFF_SIZE)
        total = (HEADER_SIZE + size + 3) & ~3
        self.flash.erase_sector(dst_sector)
        for off in range(0, total, 4):
            if self.flash.write_word(dst_base + off, self.flash.read32(src_base + off)):
                return -1
        return 0 if self.image_ok(dst_base) else -1

    def do_backup(self):
        if self.image_ok(SLOT_A_BASE):
            self.log("boot: backing up slot A")
            if self.copy_slot(BACKUP_BASE, BACKUP_SECTOR, SLOT_A_BASE):
                self.log("boot: backup failed, update cancelled")
                self.clear_flags()
                return
        else:
            self.log("boot: no valid image to back up")
            self.flash.erase_sector(BACKUP_SECTOR)
        self.set_flag(BACKUP_DONE)

    def do_install(self):
        if self.image_ok(SLOT_B_BASE):
            self.log("boot: installing slot B into slot A")
            if self.copy_slot(SLOT_A_BASE, SLOT_A_SECTOR, SLOT_B_BASE) == 0:
                self.set_flag(INSTALL_DONE)
                return
            self.log("boot: install failed")
        else:
            self.log("boot: slot B is not valid")
        self.set_flag(INSTALL_DONE)
        self.set_flag(TRIAL_STARTED)

    def do_rollback(self):
        self.log("boot: new image not confirmed, rolling back")
        if self.image_ok(BACKUP_BASE):
            if self.copy_slot(SLOT_A_BASE, SLOT_A_SECTOR, BACKUP_BASE):
                self.log("boot: restore failed")
        else:
            self.log("boot: no valid backup to restore")
        self.set_flag(ROLLBACK_DONE)

    def install_run_pending(self):
        for _ in range(8):
            action = boot_next_action(self.flags())
            if action == BACKUP:
                self.do_backup()
            elif action == INSTALL:
                self.do_install()
            elif action == START_TRIAL:
                self.log("boot: starting new image on trial")
                self.set_flag(TRIAL_STARTED)
                return
            elif action == ROLLBACK:
                self.do_rollback()
            elif action == CLEAR_STATE:
                self.clear_flags()
            else:
                return

    # -------------------------------------------------- power and reset

    def reset(self, by_watchdog=False):
        """Power-on or reset: runs boot/main.c up to the listen window."""
        self.boot_count += 1
        self.mode = "off"
        self.running_version = None
        self.up = None
        self.watchdog_running = False  # any reset stops the IWDG
        self.parser.reset()
        try:
            self.log("boot: started")
            if by_watchdog:
                self.log("boot: last reset was the watchdog")
            self.install_run_pending()
            self.mode = "bootloader"
        except PowerLoss:
            self.mode = "off"

    def update_requested(self):
        f = self.flags()
        return f["requested"] and not f["trial_started"]

    def line_quiet(self):
        """The 500 ms listen window expired. Restart to install, jump, or wait."""
        if self.mode not in ("bootloader", "waiting"):
            return
        if self.update_requested():
            self.log("boot: restarting to install")
            self.reset()
            if self.mode == "bootloader":
                self.line_quiet()
            return
        if self.mode == "waiting":
            return
        if self.image_ok(SLOT_A_BASE) and self.vectors_look_valid():
            f = self.flags()
            if f["requested"] and f["trial_started"] and not f["confirmed"]:
                self.log("boot: watchdog armed for trial")
                self.watchdog_running = True
            self.log("boot: jumping to slot A")
            try:
                self.run_app()
            except PowerLoss:
                self.mode = "off"
            return
        self.log("boot: no valid signed image in slot A, waiting for host")
        self.mode = "waiting"

    def run_app(self):
        """Models app/main.c running from slot A."""
        self.mode = "app"
        version = self.flash.read32(SLOT_A_BASE + OFF_VERSION)
        size = self.flash.read32(SLOT_A_BASE + OFF_SIZE)
        body = self.flash.read(SLOT_A_BASE + HEADER_SIZE, size)
        self.running_version = version
        self.log("app: version %d running" % version)
        if NO_CONFIRM_MARKER in body:
            self.app_kicks_watchdog = False
            self.log("app: this build never confirms itself")
            return
        self.app_kicks_watchdog = True
        f = self.flags()
        if f["requested"] and f["trial_started"] and not f["confirmed"]:
            if self.set_flag(CONFIRMED) == 0:
                self.log("app: update confirmed")
            else:
                self.log("app: could not confirm update")

    def wait(self, seconds):
        """Time passes with no host traffic. Models the watchdog timeout."""
        if self.mode == "app" and self.watchdog_running and not self.app_kicks_watchdog \
                and seconds >= WATCHDOG_TIMEOUT_S:
            self.reset(by_watchdog=True)
            if self.mode == "bootloader":
                self.line_quiet()

    # -------------------------------------------------- UART

    def receive(self, data):
        """Bytes arriving on USART2. Only the bootloader listens."""
        if self.mode not in ("bootloader", "waiting"):
            return
        for byte in data:
            result = self.parser.feed(byte)
            if result == "ready":
                try:
                    self.handle_frame(self.parser.type, bytes(self.parser.payload))
                except PowerLoss:
                    self.mode = "off"
                    return
            elif result is not None:
                self.nack(result)

    def handle_frame(self, msg_type, payload):
        if msg_type == MSG_PING:
            self.send(MSG_ACK, bytes([PROTOCOL_VERSION]))
        elif msg_type == MSG_START:
            self.on_start(payload)
        elif msg_type == MSG_DATA:
            self.on_data(payload)
        elif msg_type == MSG_END:
            self.on_end()
        else:
            self.nack(ERR_UNKNOWN_TYPE)

    def on_start(self, payload):
        self.up = None
        if len(payload) != 8:
            return self.nack(ERR_BAD_LENGTH)
        size, crc = struct.unpack("<II", payload)
        if size == 0 or size > SLOT_SIZE:
            return self.nack(ERR_TOO_BIG)
        if self.flags()["requested"]:
            return self.nack(ERR_BUSY)
        self.clear_flags()
        self.flash.erase_sector(SLOT_B_SECTOR)
        self.up = {"size": size, "crc": crc, "offset": 0}
        self.ack32(0)

    def on_data(self, payload):
        up = self.up
        if up is None:
            return self.nack(ERR_STATE)
        if len(payload) < 5:
            return self.nack(ERR_BAD_LENGTH)
        offset = struct.unpack_from("<I", payload)[0]
        data = payload[4:]
        n = len(data)
        if offset < up["offset"] and offset + n == up["offset"]:
            return self.ack32(up["offset"])  # repeat of the chunk just written
        if offset != up["offset"]:
            return self.nack(ERR_OFFSET)
        if n > up["size"] - up["offset"]:
            return self.nack(ERR_BAD_LENGTH)
        if n % 4 and offset + n != up["size"]:
            return self.nack(ERR_BAD_LENGTH)
        for i in range(0, n, 4):
            word = data[i:i + 4].ljust(4, b"\xff")
            if self.flash.write_word(SLOT_B_BASE + offset + i, struct.unpack("<I", word)[0]):
                self.up = None
                return self.nack(ERR_FLASH)
        up["offset"] += n
        self.ack32(up["offset"])

    def on_end(self):
        up = self.up
        self.up = None
        if up is None or up["offset"] != up["size"]:
            return self.nack(ERR_STATE)
        if zlib.crc32(self.flash.read(SLOT_B_BASE, up["size"])) != up["crc"]:
            return self.nack(ERR_IMAGE_CRC)
        status = self.image_status(SLOT_B_BASE)
        if status:
            return self.nack(status)
        if self.set_flag(REQUESTED):
            return self.nack(ERR_FLASH)
        self.log("update: image verified, install requested")
        self.ack32(up["size"])

    def take_output(self):
        out = bytes(self.uart_out)
        self.uart_out.clear()
        return out

    def load(self, base, image):
        """Programs an image directly, like `make flash-app` over SWD."""
        sector = {SLOT_A_BASE: SLOT_A_SECTOR, SLOT_B_BASE: SLOT_B_SECTOR,
                  BACKUP_BASE: BACKUP_SECTOR}[base]
        start, size = SECTORS[sector]
        self.flash.mem[start:start + size] = b"\xff" * size
        self.flash.mem[start:start + len(image)] = image

# ---------------------------------------------------------------- crypto


def verify_signature(public_key_xy, message, raw_sig):
    """ECDSA P-256 over SHA-256, signature as r||s big-endian (micro-ecc format)."""
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

    x = int.from_bytes(public_key_xy[:32], "big")
    y = int.from_bytes(public_key_xy[32:], "big")
    key = ec.EllipticCurvePublicNumbers(x, y, ec.SECP256R1()).public_key()
    r = int.from_bytes(raw_sig[:32], "big")
    s = int.from_bytes(raw_sig[32:], "big")
    if r == 0 or s == 0:
        return False
    try:
        key.verify(encode_dss_signature(r, s), message, ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def public_key_from_pem(path):
    from cryptography.hazmat.primitives import serialization
    with open(path, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    n = key.public_key().public_numbers()
    return n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")


def public_key_from_c(path):
    """Reads the 64 key bytes out of boot/pubkey.c."""
    with open(path) as f:
        values = re.findall(r"0x([0-9a-fA-F]{2})", f.read())
    if len(values) != 64:
        raise ValueError("expected 64 key bytes in " + path)
    return bytes(int(v, 16) for v in values)

# ---------------------------------------------------------------- TCP server


LISTEN_WINDOW_S = 0.5


def serve(device, port):
    """Exposes the device's UART on a TCP port.

    A new connection counts as pressing the reset button, so `fwtool.py ping`
    or `send` reach the bootloader during its listen window, as on hardware.
    Device log text is also printed here.
    """
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(1)
    print("simulator listening on socket://localhost:%d" % port, flush=True)
    while True:
        conn, _ = server.accept()
        conn.settimeout(0.02)
        print("--- host connected: reset", flush=True)
        device.reset()
        last_rx = time.monotonic()
        try:
            while True:
                try:
                    data = conn.recv(4096)
                    if not data:
                        break
                    device.receive(data)
                    last_rx = time.monotonic()
                except socket.timeout:
                    pass
                idle = time.monotonic() - last_rx
                if idle > LISTEN_WINDOW_S and device.mode in ("bootloader", "waiting"):
                    device.line_quiet()
                    last_rx = time.monotonic()
                elif idle > WATCHDOG_TIMEOUT_S and device.mode == "app":
                    device.wait(idle)
                    last_rx = time.monotonic()
                out = device.take_output()
                if out:
                    conn.sendall(out)
                    for line in re.findall(rb"(?:boot|app|update): [ -~]*", out):
                        print(line.decode(), flush=True)
        except (ConnectionResetError, BrokenPipeError):
            pass
        conn.close()
        print("--- host disconnected", flush=True)


def main():
    parser = argparse.ArgumentParser(description="STM32 bootloader simulator")
    key = parser.add_mutually_exclusive_group()
    key.add_argument("--pubkey-from", metavar="PEM", help="private key whose public half to trust")
    key.add_argument("--pubkey-c", metavar="C_FILE", default="boot/pubkey.c",
                     help="public key C file (default: boot/pubkey.c)")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--slot-a", metavar="IMAGE", help="signed image preloaded into slot A")
    args = parser.parse_args()

    pub = public_key_from_pem(args.pubkey_from) if args.pubkey_from else public_key_from_c(args.pubkey_c)
    device = Device(pub)
    if args.slot_a:
        with open(args.slot_a, "rb") as f:
            device.load(SLOT_A_BASE, f.read())
    serve(device, args.port)


if __name__ == "__main__":
    main()
