import argparse
import hashlib
import struct
import sys

MAGIC = 0x31495746  # "FWI1"
HEADER_SIZE = 512
SIGNATURE_SIZE = 64


def cmd_pack(args):
    with open(args.input, "rb") as f:
        body = f.read()
    digest = hashlib.sha256(body).digest()
    header = struct.pack("<IIII", MAGIC, args.version, len(body), 0)
    header += digest + bytes(SIGNATURE_SIZE)
    header += b"\xff" * (HEADER_SIZE - len(header))
    with open(args.output, "wb") as f:
        f.write(header + body)
    print("packed %s: version %d, %d bytes of code, sha256 %s"
          % (args.output, args.version, len(body), digest.hex()))
    return 0


def main():
    parser = argparse.ArgumentParser(description="Firmware image tool")
    sub = parser.add_subparsers(dest="command", required=True)
    pack = sub.add_parser("pack", help="add a header to a raw binary")
    pack.add_argument("input")
    pack.add_argument("output")
    pack.add_argument("--version", type=int, default=1)
    pack.set_defaults(func=cmd_pack)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
