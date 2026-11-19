#!/usr/bin/env python3
"""Run the shared LVGL UI on macOS, using Codex quota or explicit test data."""
import argparse
import asyncio
from contextlib import suppress
from datetime import datetime
import json
from pathlib import Path
import signal
import time

from quota_bridge import CodexClient, normalize_buckets, resolve_codex, TZ

PROJECT = Path(__file__).resolve().parents[1]
SOCKET = PROJECT / ".runtime" / "simulator.sock"
EXECUTABLE = PROJECT / "build-simulator" / "assistant_simulator.app" / "Contents" / "MacOS" / "assistant_simulator"
SCENARIOS = ("live", "normal", "low", "unavailable", "stale")


def log(message):
    line = f"{datetime.now(TZ):%H:%M:%S} {message}"
    print(line, flush=True)
    path = PROJECT / ".runtime" / "simulator.log"
    path.parent.mkdir(exist_ok=True)
    if path.exists() and path.stat().st_size > 1024 * 1024:
        path.write_text("")
    with path.open("a") as output:
        output.write(line + "\n")


def fixture_buckets(scenario, now):
    def window(used, minutes, seconds):
        reset = now + seconds
        return {"used": used, "minutes": minutes, "reset": reset,
                "reset_local": datetime.fromtimestamp(reset, TZ).strftime("%m/%d %H:%M UTC+8")}
    used = 95 if scenario == "low" else 38
    primary = None if scenario == "unavailable" else window(used, 10080, 3661)
    return {
        "test-weekly": {"bucket": f"TEST {scenario}", "plan": "fixture", "primary": primary,
                        "secondary": None, "credits": "--", "reset_credits": None},
        "test-short": {"bucket": "TEST alternate", "plan": "fixture", "primary": window(25, 300, 123),
                       "secondary": window(60, 10080, 86400), "credits": "123.4", "reset_credits": 2},
    }


class Simulator:
    def __init__(self, args):
        self.args = args
        self.mode = "normal" if args.mock else args.scenario
        self.stopping, self.refresh = asyncio.Event(), asyncio.Event()
        self.client = self.process = self.server = None
        self.buckets, self.selected, self.fetched_at, self.seq = {}, "", 0, 0

    async def control(self, reader, writer):
        try:
            line = (await asyncio.wait_for(reader.readline(), 2)).decode().strip()
            if line == "stop":
                self.stopping.set()
            elif line.startswith("scenario ") and line[9:] in SCENARIOS:
                await self.set_mode(line[9:])
            elif line.startswith("ui "):
                await self.write("@UI " + line[3:])
            elif line.startswith("hardware "):
                await self.write("@HW " + line[9:])
            elif line == "refresh":
                self.refresh.set()
            writer.write((f"simulator {self.mode}\n").encode())
            await writer.drain()
        except (ConnectionError, asyncio.TimeoutError):
            pass
        finally:
            writer.close()
            with suppress(ConnectionError):
                await writer.wait_closed()

    async def write(self, line):
        if self.process and self.process.returncode is None:
            self.process.stdin.write((line + "\n").encode())
            await self.process.stdin.drain()

    async def set_mode(self, mode):
        self.mode = mode
        self.buckets, self.selected = {}, ""
        await self.write("@SIM_MODE " + ("LIVE ACCOUNT" if mode == "live" else "TEST " + mode))
        log("MODE " + mode)
        self.refresh.set()

    async def send_snapshot(self):
        if not self.buckets:
            return
        self.seq = self.seq % 0xFFFFFFFF + 1
        now = int(time.time())
        age = max(0, now - self.fetched_at)
        if self.mode == "stale":
            age += 91
        frame = {"type": "quota", "version": 1, "seq": self.seq,
                 "now": now, "age": age, **self.buckets[self.selected]}
        await self.write(json.dumps(frame, separators=(",", ":"), allow_nan=False))

    async def poll(self):
        while not self.stopping.is_set():
            await self.refresh.wait()
            self.refresh.clear()
            mode = self.mode
            try:
                if mode == "live":
                    if self.client is None:
                        self.client = CodexClient(resolve_codex(self.args.codex))
                    buckets = normalize_buckets(await self.client.quota())
                else:
                    buckets = fixture_buckets(mode, int(time.time()))
                if self.mode != mode:
                    continue
                self.buckets = buckets
                if self.selected not in buckets:
                    self.selected = next(iter(buckets))
                self.fetched_at = int(time.time())
                primary = buckets[self.selected]["primary"]
                remaining = f"{100 - primary['used']:.1f}%" if primary else "unavailable"
                log(f"FETCH_OK mode={mode} bucket={self.selected} remaining={remaining}")
                await self.send_snapshot()
            except (RuntimeError, OSError, asyncio.TimeoutError, ValueError):
                log("FETCH_FAILED: quota unavailable; existing data will become STALE")
                if self.client:
                    await self.client.close()

    async def interval(self):
        while True:
            await asyncio.sleep(60)
            if self.mode == "live":
                self.refresh.set()

    async def read_ui(self):
        while line := await self.process.stdout.readline():
            text = line.decode(errors="replace").strip()
            if text == "@CODEX_REFRESH":
                log("MOUSE_REFRESH")
                self.refresh.set()
            elif text == "@CODEX_NEXT":
                log("MOUSE_NEXT")
                if self.buckets:
                    keys = list(self.buckets)
                    self.selected = keys[(keys.index(self.selected) + 1) % len(keys)]
                    await self.send_snapshot()
            elif text.startswith("@HW_SCENARIO "):
                await self.write("@HW " + text[13:])
            elif text.startswith("@SIM_SCENARIO "):
                await self.set_mode(text[14:])
            else:
                log(text)
        if self.process.returncode is not None and self.process.returncode != 0:
            raise RuntimeError(f"Native simulator exited with code {self.process.returncode}")

    async def run(self):
        self.server = await asyncio.start_unix_server(self.control, path=str(SOCKET))
        tasks = []
        try:
            self.process = await asyncio.create_subprocess_exec(
                str(EXECUTABLE), cwd=str(PROJECT), stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
            loop = asyncio.get_running_loop()
            for sig in (signal.SIGINT, signal.SIGTERM):
                loop.add_signal_handler(sig, self.stopping.set)
            await self.set_mode(self.mode)
            tasks = [asyncio.create_task(coro) for coro in (
                self.read_ui(), self.poll(), self.interval(), self.process.wait(), self.stopping.wait())]
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
            if self.process.returncode is not None and self.process.returncode != 0:
                raise RuntimeError(f"Native simulator exited with code {self.process.returncode}")
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if self.process and self.process.returncode is None:
                self.process.terminate()
                with suppress(asyncio.TimeoutError):
                    await asyncio.wait_for(self.process.wait(), 3)
                if self.process.returncode is None:
                    self.process.kill()
                    await self.process.wait()
            if self.client:
                await self.client.close()
            self.server.close()
            await self.server.wait_closed()
            SOCKET.unlink(missing_ok=True)
            log("SIMULATOR_STOPPED")


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mock", action="store_true", help="start with clearly marked test data")
    parser.add_argument("--scenario", choices=SCENARIOS, default="live")
    parser.add_argument("--stop", action="store_true")
    parser.add_argument("--codex")
    args = parser.parse_args()
    SOCKET.parent.mkdir(exist_ok=True)
    try:
        reader, writer = await asyncio.open_unix_connection(str(SOCKET))
    except (OSError, ConnectionError):
        SOCKET.unlink(missing_ok=True)
        if args.stop:
            log("Simulator is not running")
            return
    else:
        requested = "normal" if args.mock else args.scenario
        writer.write(b"stop\n" if args.stop else f"scenario {requested}\n".encode())
        await writer.drain()
        response = (await asyncio.wait_for(reader.readline(), 2)).decode().strip()
        writer.close()
        await writer.wait_closed()
        log("Stop requested" if args.stop else f"Using existing window ({response})")
        return
    if not EXECUTABLE.is_file():
        raise RuntimeError("Build simulator first: ./scripts/simulator.sh build")
    await Simulator(args).run()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (RuntimeError, OSError, asyncio.TimeoutError) as error:
        log(f"SIMULATOR_FAILED: {error}")
        raise SystemExit(1)
