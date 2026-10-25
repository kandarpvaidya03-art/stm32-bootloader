import argparse
import hashlib
import os
import struct
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "sim"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import stm32sim as sim  # noqa: E402
import fwtool  # noqa: E402


@pytest.fixture(autouse=True)
def no_host_delays(monkeypatch):
    """fwtool.py sleeps to let a real UART settle; the simulator does not need it."""
    monkeypatch.setattr(fwtool.time, "sleep", lambda seconds: None)


@pytest.fixture(scope="session")
def key():
    from cryptography.hazmat.primitives.asymmetric import ec
    return ec.generate_private_key(ec.SECP256R1())


@pytest.fixture(scope="session")
def other_key():
    from cryptography.hazmat.primitives.asymmetric import ec
    return ec.generate_private_key(ec.SECP256R1())


def public_xy(key):
    n = key.public_key().public_numbers()
    return n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big")


def app_body(version, confirms=True, size=256):
    """A stand-in application: a valid vector table followed by filler.

    The simulator only looks at the vector table and at whether the image
    contains the string that the never-confirming build prints.
    """
    vectors = sim.SLOT_A_BASE + sim.HEADER_SIZE
    body = struct.pack("<II", sim.RAM_END, vectors + 0x41)  # SP, reset handler (Thumb)
    body += b"app: version %d" % version
    if not confirms:
        body += sim.NO_CONFIRM_MARKER
    return body.ljust(size, b"\x00")


def pack(body, version):
    """Same header layout as tools/imgtool.py pack."""
    header = struct.pack("<IIII", sim.IMAGE_MAGIC, version, len(body), 0)
    header += hashlib.sha256(body).digest() + bytes(64)
    header += b"\xff" * (sim.HEADER_SIZE - len(header))
    return bytearray(header + body)


def sign(image, key):
    """Same signature as tools/imgtool.py sign."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    der = key.sign(bytes(image[:sim.SIGNED_SIZE]), ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    image[sim.OFF_SIG:sim.OFF_SIG + 64] = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    return image


@pytest.fixture
def make_image(key):
    def make(version, confirms=True, signed=True, signing_key=None):
        image = pack(app_body(version, confirms), version)
        if signed:
            sign(image, signing_key or key)
        return bytes(image)
    return make


class SimPort:
    """Port-like object that connects fwtool.Link to a simulated device.

    Hooks let a test corrupt or drop traffic in either direction.
    """

    def __init__(self, device):
        self.device = device
        self.rx = bytearray()
        self.to_device = None    # function(bytes) -> bytes, to tamper with host frames
        self.from_device = None  # function(bytes) -> bytes, to tamper with replies

    def write(self, data):
        if self.device.mode == "off":
            raise SystemExit("board is not powered")  # host gives up at once
        if self.to_device:
            data = self.to_device(data)
        self.device.receive(data)
        out = self.device.take_output()
        if self.from_device:
            out = self.from_device(out)
        self.rx += out

    def read(self, n):
        if not self.rx and self.device.mode == "off":
            raise SystemExit("board lost power")  # no reply will ever come
        chunk = bytes(self.rx[:n])
        del self.rx[:n]
        return chunk

    def reset_input_buffer(self):
        self.rx.clear()


@pytest.fixture
def device(key):
    """A board with the bootloader and a signed version 1 already flashed."""
    dev = sim.Device(public_xy(key))
    image = pack(app_body(1), 1)
    dev.load(sim.SLOT_A_BASE, bytes(sign(image, key)))
    return dev


def send(port, image_bytes, tmp_path):
    """Runs fwtool.py's real send command against the simulated port."""
    path = tmp_path / "image.bin"
    path.write_bytes(image_bytes)
    link = fwtool.Link(port)
    return fwtool.cmd_send(link, argparse.Namespace(image=str(path), watch=0))


def boot(dev):
    """Reset, then let the listen window expire, like an unattended power-on."""
    dev.reset()
    dev.line_quiet()
    return dev.take_output().decode(errors="replace")
