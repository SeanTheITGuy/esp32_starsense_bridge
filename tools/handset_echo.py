#!/usr/bin/env python3
"""Send only the Celestron Kx echo request; no motion commands."""
import argparse
from contextlib import contextmanager
import socket
import time
import serial


@contextmanager
def transport(args):
    if args.bluetooth:
        with socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM,
                           socket.BTPROTO_RFCOMM) as link:
            link.settimeout(10)
            link.connect((args.bluetooth, args.channel))
            link.settimeout(0.2)

            def read(count):
                try:
                    data = link.recv(count)
                except TimeoutError:
                    return b''
                if not data:
                    raise RuntimeError('Bluetooth link closed')
                return data

            def write(data):
                link.sendall(data)
                return len(data)

            yield write, read
    else:
        port = serial.Serial(port=None, baudrate=9600, bytesize=serial.EIGHTBITS,
                             parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                             timeout=0.2, write_timeout=2, xonxoff=False,
                             rtscts=False, dsrdtr=False)
        port.dtr = False
        port.rts = False
        port.port = args.port or '/dev/ttyUSB0'
        with port:
            time.sleep(0.5)
            port.reset_input_buffer()
            yield port.write, port.read


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', nargs='?', help='serial device (default /dev/ttyUSB0)')
    parser.add_argument('--bluetooth', metavar='MAC', help='Linux direct RFCOMM socket instead of a serial device')
    parser.add_argument('--channel', type=int, default=1, help='SPP channel; verify using SDP first')
    parser.add_argument('--rounds', type=int, default=1)
    args = parser.parse_args()
    if args.port and args.bluetooth:
        parser.error('choose a serial port or --bluetooth, not both')
    if args.rounds < 1 or not 1 <= args.channel <= 30:
        parser.error('rounds must be positive and channel must be 1..30')
    with transport(args) as (write, read):
        for index in range(args.rounds):
            if write(b'Kx') != 2:
                raise RuntimeError('short write')
            answer = bytearray()
            deadline = time.monotonic() + 3
            while len(answer) < 2 and time.monotonic() < deadline:
                answer.extend(read(2 - len(answer)))
            answer.extend(read(32))
            print(f'round {index + 1}: TX 4b 78 (Kx); RX {bytes(answer)!r} [{answer.hex(" ")}]')
            if answer != b'x#':
                raise SystemExit('FAIL: expected exactly x#; check bridge readiness, baud and handset state')
    print(f'PASS: {args.rounds} exact Kx -> x# transactions')


if __name__ == '__main__':
    main()
