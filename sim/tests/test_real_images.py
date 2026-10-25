"""Update and rollback using the real application images from `make`.

Skipped unless build/app-*-image.bin exist. The images are signed here with a
throwaway test key, so no private key is needed (CI runs this after `make`).
"""

import os

import pytest

import stm32sim as sim
from conftest import ROOT, SimPort, boot, send, sign, public_xy


def built(name):
    path = os.path.join(ROOT, "build", "app-%s-image.bin" % name)
    if not os.path.exists(path):
        pytest.skip("run `make` first to build " + path)
    with open(path, "rb") as f:
        return bytearray(f.read())


@pytest.fixture
def board(key):
    dev = sim.Device(public_xy(key))
    dev.load(sim.SLOT_A_BASE, bytes(sign(built("v1"), key)))
    return dev


def test_real_v2_installs(board, key, tmp_path):
    boot(board)
    assert board.running_version == 1  # vector table of the real build is accepted
    board.reset()
    send(SimPort(board), bytes(sign(built("v2"), key)), tmp_path)
    board.line_quiet()
    assert board.running_version == 2
    assert "app: update confirmed" in board.take_output().decode()


def test_real_bad_build_rolls_back(board, key, tmp_path):
    board.reset()
    send(SimPort(board), bytes(sign(built("bad"), key)), tmp_path)
    board.line_quiet()
    assert board.running_version == 3
    boot(board)
    assert board.running_version == 1
