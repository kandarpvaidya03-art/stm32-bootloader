"""End-to-end update tests: the real fwtool.py against the simulated board."""

import struct

import pytest

import stm32sim as sim
import fwtool
from conftest import SimPort, boot, send


def flags_clear(dev):
    return not any(dev.flags().values())


def test_boots_preloaded_image(device):
    out = boot(device)
    assert "jumping to slot A" in out
    assert device.mode == "app" and device.running_version == 1


def test_update_installs_and_confirms(device, make_image, tmp_path):
    boot(device)
    device.reset()  # the user presses reset when fwtool asks
    assert send(SimPort(device), make_image(2), tmp_path) == 0

    device.line_quiet()  # host goes quiet: bootloader restarts and installs
    out = device.take_output().decode()
    for step in ("backing up slot A", "installing slot B", "starting new image on trial",
                 "app: version 2 running", "app: update confirmed"):
        assert step in out
    assert device.running_version == 2

    # Next power-on: the state is cleared and version 2 keeps running.
    boot(device)
    assert device.running_version == 2 and flags_clear(device)


def test_unconfirmed_image_rolls_back(device, make_image, tmp_path):
    device.reset()
    send(SimPort(device), make_image(3, confirms=False), tmp_path)
    device.line_quiet()
    assert device.running_version == 3  # on trial

    out = boot(device)  # reset before it confirmed
    assert "rolling back" in out
    assert device.running_version == 1

    boot(device)
    assert device.running_version == 1 and flags_clear(device)


@pytest.mark.parametrize("case, code", [
    ("unsigned", sim.ERR_SIGNATURE),
    ("wrong key", sim.ERR_SIGNATURE),
    ("version changed after signing", sim.ERR_SIGNATURE),
    ("code changed after signing", sim.ERR_IMAGE_INVALID),
    ("no header", sim.ERR_IMAGE_INVALID),
])
def test_bad_images_are_refused(device, make_image, other_key, tmp_path, capsys, case, code):
    image = bytearray(make_image(2))
    if case == "unsigned":
        image = bytearray(make_image(2, signed=False))
    elif case == "wrong key":
        image = bytearray(make_image(2, signing_key=other_key))
    elif case == "version changed after signing":
        image[sim.OFF_VERSION] ^= 0x01
    elif case == "code changed after signing":
        image[-1] ^= 0x01
    elif case == "no header":
        image = image[sim.HEADER_SIZE:]

    device.reset()
    with pytest.raises(SystemExit) as exc:
        send(SimPort(device), bytes(image), tmp_path)
    assert "error 0x%02x" % code in str(exc.value)

    # Nothing was installed and the old image still runs.
    boot(device)
    assert device.running_version == 1 and flags_clear(device)


def test_truncated_transfer_leaves_device_updatable(device, make_image, tmp_path):
    image = make_image(2)
    device.reset()
    port = SimPort(device)
    link = fwtool.Link(port)
    fwtool.connect(link)
    fwtool.request(link, fwtool.MSG_START, struct.pack("<II", len(image), 0), timeout=1)
    fwtool.request(link, fwtool.MSG_DATA, struct.pack("<I", 0) + image[:252], timeout=1)
    with pytest.raises(SystemExit) as exc:
        fwtool.request(link, fwtool.MSG_END, b"", timeout=1)  # host gives up early
    assert "error 0x%02x" % sim.ERR_STATE in str(exc.value)

    boot(device)
    assert device.running_version == 1

    device.reset()
    send(SimPort(device), image, tmp_path)  # a complete retry works
    device.line_quiet()
    assert device.running_version == 2


def test_lost_reply_is_retried_without_writing_twice(device, make_image, tmp_path):
    """The device's ACK for one DATA frame is lost; the host resends the frame."""
    port = SimPort(device)
    state = {"acks": 0}

    def drop_fifth_ack(out):
        if out[:1] == bytes([sim.SOF]) and out[1:2] == bytes([sim.MSG_ACK]):
            state["acks"] += 1
            if state["acks"] == 5:
                return b""
        return out

    port.from_device = drop_fifth_ack
    device.reset()
    assert send(port, make_image(2), tmp_path) == 0
    device.line_quiet()
    assert device.running_version == 2


def test_corrupted_frame_is_retried(device, make_image, tmp_path):
    """A bit flips in a DATA frame on the wire. The device NACKs with a CRC error
    and the host sends the frame again instead of giving up."""
    port = SimPort(device)
    state = {"frames": 0}

    def flip_bit_in_third_data_frame(data):
        if data[1:2] == bytes([sim.MSG_DATA]):
            state["frames"] += 1
            if state["frames"] == 3:
                data = bytearray(data)
                data[20] ^= 0x10
                return bytes(data)
        return data

    port.to_device = flip_bit_in_third_data_frame
    device.reset()
    assert send(port, make_image(2), tmp_path) == 0
    device.line_quiet()
    assert device.running_version == 2


def test_busy_while_update_pending(device, make_image, tmp_path):
    device.reset()
    send(SimPort(device), make_image(2), tmp_path)
    # Before the device restarts to install, a second START is refused.
    port = SimPort(device)
    link = fwtool.Link(port)
    with pytest.raises(SystemExit) as exc:
        fwtool.request(link, fwtool.MSG_START, struct.pack("<II", 100, 0), timeout=1)
    assert "error 0x%02x" % sim.ERR_BUSY in str(exc.value)
