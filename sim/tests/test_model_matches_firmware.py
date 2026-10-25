"""Keeps the simulator honest: its boot decision must match the C firmware's.

common/bootplan.c is compiled for the host and called through ctypes, then
compared with stm32sim.boot_next_action() for all 64 flag combinations.
"""

import ctypes
import itertools
import os
import shutil
import subprocess

import pytest

import stm32sim as sim

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class BootFlags(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint8) for name in sim.FLAG_NAMES]


@pytest.fixture(scope="module")
def bootplan(tmp_path_factory):
    if shutil.which("gcc") is None:
        pytest.skip("gcc not available")
    lib = tmp_path_factory.mktemp("lib") / "libbootplan.so"
    subprocess.run(["gcc", "-shared", "-fPIC", "-I", os.path.join(ROOT, "common"),
                    os.path.join(ROOT, "common", "bootplan.c"), "-o", str(lib)], check=True)
    so = ctypes.CDLL(str(lib))
    so.boot_next_action.argtypes = [ctypes.POINTER(BootFlags)]
    so.boot_next_action.restype = ctypes.c_int
    return so


def test_all_64_flag_states_match(bootplan):
    for bits in itertools.product([0, 1], repeat=len(sim.FLAG_NAMES)):
        flags = dict(zip(sim.FLAG_NAMES, map(bool, bits)))
        c_flags = BootFlags(*bits)
        assert bootplan.boot_next_action(ctypes.byref(c_flags)) == sim.boot_next_action(flags), flags
