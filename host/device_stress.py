#!/usr/bin/env python3
"""Exercise real UI navigation without enabling a radio or microphone."""
import argparse
import re
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--cycles", type=int, default=100)
    args = parser.parse_args()
    names = ["quota", "clock", "timer", "sensors", "assistant", "settings"]
    transcript = bytearray()
    with serial.Serial(args.port, 115200, timeout=0.02) as connection:
        def drain(seconds):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                data = connection.read(connection.in_waiting or 1)
                transcript.extend(data)
                if data:
                    print(data.decode("utf-8", errors="replace"), end="", flush=True)
        def command(value, seconds=0.3):
            connection.write(("@UI " + value + "\n").encode())
            drain(seconds)
        command("page 10")
        command("set display.timeout 3")
        command("set display.animations 1")
        command("page 5")
        command("set timer.mode 1")
        command("click timer.reset")
        command("click timer.toggle")
        command("page 0")
        command("state")
        for index in range(args.cycles):
            app = index % 6
            command(f"app {app}")
            command("click nav.2")
            command(f"click recent.close.{names[app]}")
            command("click nav.1")
            if (index + 1) % 10 == 0:
                print(f"HOST_STRESS cycles={index+1}", flush=True)
                command("state")
        drain(11)
        command("state")
        command("page 5")
        command("click timer.reset")
        command("set timer.mode 0")
        command("page 10")
        command("set display.timeout 2")
        command("page 0")
    output = transcript.decode("utf-8", errors="replace")
    closed = re.findall(r"APP_CLOSE (?:quota|clock|timer|sensors|assistant|settings)", output)
    failures = re.findall(r"Guru Meditation|assert failed|CORRUPT HEAP|ESP_ERR_NO_MEM|Draw bitmap failed|Backtrace:", output)
    timer = [int(value) for value in re.findall(r"DEVICE_STATE .*animations=1 timer_running=1 timer_ms=(\d+)", output)]
    if len(closed) != args.cycles or failures or not timer or timer[-1] < args.cycles * 1000:
        raise SystemExit(f"DEVICE_STRESS_FAIL close_events={len(closed)} failures={failures}")
    print(f"DEVICE_STRESS_OK cycles={args.cycles} close_events={len(closed)} timer_ms={timer[-1]}", flush=True)


if __name__ == "__main__":
    main()
