import sys
import serial

port = serial.Serial(sys.argv[1], 115200, timeout=0.5)
print("listening on", sys.argv[1], "- press Ctrl+C to stop", flush=True)
try:
    while True:
        data = port.read(256)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
except KeyboardInterrupt:
    pass
