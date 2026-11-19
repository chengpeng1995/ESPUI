#!/usr/bin/env python3
"""Local USB developer console; commands execute on the device's UI thread.

Run with the ESP-IDF Python environment (pyserial). Stop the quota bridge first.
Never include Wi-Fi passwords in a recorded command list.
"""
import argparse
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("commands", nargs="*", help="UI commands, e.g. 'bench scroll' 'state'")
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--delay", type=float, default=0.6)
    parser.add_argument("--watch", type=float, default=2)
    parser.add_argument("--startup-wait", type=float, default=0.2)
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.05) as connection:
        def drain(seconds):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                data = connection.read(connection.in_waiting or 1)
                if data:
                    print(data.decode("utf-8", errors="replace"), end="", flush=True)
        drain(args.startup_wait)
        for command in args.commands:
            connection.write(("@UI " + command + "\n").encode())
            drain(args.delay)
        drain(args.watch)


if __name__ == "__main__":
    main()
