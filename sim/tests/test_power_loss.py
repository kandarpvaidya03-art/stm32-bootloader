"""Power-loss injection: cut power at every flash operation of a full update.

For each operation index k, a fresh board running version 1 receives version 2.
Power is cut at operation k (an erase leaves random data, a word write leaves
some bits unprogrammed). Power then comes back and the board is reset a few
times. The board must end up running a valid signed image, version 1 or 2,
and keep running it on later resets.
"""

import pytest

import stm32sim as sim
from conftest import SimPort, boot, send, public_xy, pack, sign, app_body

MAX_RESETS = 6


def fresh_board(key, seed):
    dev = sim.Device(public_xy(key), seed=seed)
    dev.load(sim.SLOT_A_BASE, bytes(sign(pack(app_body(1), 1), key)))
    return dev


def run_update(dev, image, tmp_path):
    """Host side of an update. Returns normally even if the board dies mid-way."""
    dev.reset()
    try:
        send(SimPort(dev), image, tmp_path)
    except SystemExit:
        pass  # the board stopped answering
    dev.line_quiet()


def count_flash_ops(key, image, tmp_path, confirms):
    dev = fresh_board(key, 0)
    run_update(dev, image, tmp_path)
    boot(dev)  # includes the clean-up after confirm or rollback
    return dev.flash.ops


def outcome_after_power_returns(dev):
    versions = []
    for _ in range(MAX_RESETS):
        boot(dev)
        versions.append(dev.running_version)
    return versions


@pytest.mark.parametrize("confirms", [True, False], ids=["good-image", "never-confirms"])
def test_power_loss_at_every_flash_operation(key, make_image, tmp_path, confirms):
    image = make_image(2, confirms=confirms)
    total = count_flash_ops(key, image, tmp_path, confirms)
    assert total > 100  # the transfer alone is dozens of word writes

    failures = []
    for k in range(total):
        dev = fresh_board(key, seed=k)
        dev.flash.cut_power_at = k
        run_update(dev, image, tmp_path)
        boot(dev)  # covers a cut during the post-update clean-up as well
        versions = outcome_after_power_returns(dev)
        final = versions[-1]
        stable = len(set(versions[-3:])) == 1
        if final not in (1, 2) or not stable:
            failures.append((k, versions))

    assert not failures, "%d of %d cut points left the board without a stable image, e.g. %s" % (
        len(failures), total, failures[:5])
