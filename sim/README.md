# Bootloader simulator

`stm32sim.py` is a Python model of the NUCLEO-F401RE running this bootloader. It lets the host tools and the update, install and rollback logic run without a board, and it can cut power at any flash operation, which is hard to do repeatably on hardware.

## What is modelled

| Part | How |
|---|---|
| Flash | 512 KB with the F401RE's sector map. Erase sets a sector to `0xFF`; programming can only clear bits. |
| Power loss | Can be injected at any erase or word write. An interrupted erase leaves random data; an interrupted word write leaves some bits unprogrammed. |
| Boot state | The six write-once flag words in sector 3, read and written as in `common/bootstate.c` |
| Boot decision | Same rules as `common/bootplan.c`; a test compiles the C file and compares all 64 flag states |
| Install, backup, rollback | Same steps and order as `boot/install.c` |
| UART protocol | Byte-wise frame parser and the START / DATA / END handler from `boot/update.c`, with the same error codes |
| Image checks | Magic, size, SHA-256, and ECDSA P-256 over the first 48 header bytes |
| Application | Not executed. The vector table is checked as in `boot/main.c`, then the sample app's behaviour is modelled: it prints its version and confirms itself, unless the image contains the string the non-confirming build prints. |

The model is a second implementation of the same logic, so it can drift from the C code. The bootplan comparison test catches drift in the decision rules; the rest has to be kept in step by hand. A passing simulation is evidence about the design, not a substitute for testing on the board.

## Running the tests

```bash
pip install pyserial cryptography pytest
make                          # optional: also tests the real application images
python -m pytest sim/tests
```

| Test file | Covers |
|---|---|
| `test_update.py` | Update and confirm, rollback, five kinds of bad image, truncated transfer, a lost ACK, a corrupted frame, a second START while an install is pending |
| `test_power_loss.py` | Power cut at every flash operation of a full update (585 cut points with a good image, 778 with one that never confirms). After power returns the board must run version 1 or 2 and keep running it. |
| `test_model_matches_firmware.py` | Simulator decision rules against the compiled `common/bootplan.c` |
| `test_real_images.py` | The same flow with the application images built by `make`, signed with a throwaway key |

The tests drive the real `tools/fwtool.py` code, not a copy of it.

## Using it with fwtool.py

```bash
python sim/stm32sim.py --pubkey-from keys/signing_key.pem --slot-a build/app-v1-signed.bin
python tools/fwtool.py socket://localhost:5555 send build/app-v2-signed.bin --watch 3
```

Each new connection counts as pressing the reset button. The simulator prints the board's log lines as they happen.

## What it found

- `fwtool.py` gave up when the device reported a frame CRC error, so one flipped bit on the wire ended the update. It now resends frames that were damaged in transit (NACK `0x02` or `0x03`).
- Cutting power at each of the 585 and 778 flash operations of an update never left the board without a working image. A deliberately planted ordering bug (setting `rollback_done` before the restore) made 193 of 778 cut points fail, which shows the test can detect this class of mistake.
