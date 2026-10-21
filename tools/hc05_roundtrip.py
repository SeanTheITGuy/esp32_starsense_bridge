#!/usr/bin/env python3
"""Stage-2 binary echo check; requires pyserial. Never use with telescope attached."""
import argparse
import random
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="e.g. /dev/rfcomm0")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--rounds", type=int, default=20)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    rng = random.Random(2303)
    with serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=5,
                       xonxoff=False, rtscts=False, dsrdtr=False) as port:
        port.reset_input_buffer()
        for index in range(args.rounds):
            payload = bytes(range(256)) if index == 0 else bytes(rng.randrange(256) for _ in range(rng.randint(1, 256)))
            if port.write(payload) != len(payload):
                raise RuntimeError("short write")
            answer = bytearray()
            deadline = time.monotonic() + 5 + len(payload) * 20 / args.baud
            while len(answer) < len(payload) and time.monotonic() < deadline:
                answer.extend(port.read(len(payload) - len(answer)))
            if answer != payload:
                raise RuntimeError(f"round {index + 1}: mismatch, sent {payload.hex()}, received {answer.hex()}")
            if port.read(1):
                raise RuntimeError("unexpected extra bytes")
            print(f"round {index + 1}: {len(payload)} bytes OK")
    print("PASS: binary echo preserved in both directions")


if __name__ == "__main__":
    main()
