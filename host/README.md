# FwTool (C#)

A .NET 8 version of `tools/fwtool.py`: it sends signed firmware to the bootloader over the ST-Link's virtual COM port and prints the board's log while it installs, trial-boots and confirms. It shows how a PC application written in .NET would integrate with this firmware's update protocol.

```bash
dotnet run --project host/FwTool -- COM24 ping
dotnet run --project host/FwTool -- COM24 send build/app-v2-signed.bin
dotnet run --project host/FwTool -- socket://localhost:5555 send build/app-v2-signed.bin   # simulator
```

| File | Contents |
|---|---|
| `Crc32.cs` | CRC-32 matching `common/crc32.c` and `zlib.crc32` |
| `Frame.cs` | Frame encoder, and a reader that picks valid frames out of a stream mixed with log text |
| `Updater.cs` | PING, START, DATA, END with retries; resends frames the device reports as damaged |
| `Transport.cs`, `SerialTransport.cs` | Serial port (115200 8N1) or TCP to the simulator behind one interface |

Exit codes: 0 success, 1 the device refused the image (the error code is printed), 2 no reply.

`host/integration_test.sh` runs the tool against the [simulator](../sim/README.md): an update, a refused unsigned image, and a hung image that the watchdog rolls back. CI runs it on every push. On hardware it has been tested with the simulator only so far, not with a board.

Building with `-p:NoSerial=true` leaves out the serial port, so the tool builds without fetching the `System.IO.Ports` package; it can then only reach the simulator.
