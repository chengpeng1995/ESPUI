#!/usr/bin/env python3
"""Read Codex quota through its local app-server and send bounded JSON over USB."""
import argparse
import asyncio
from contextlib import suppress
from datetime import datetime
from decimal import Decimal, InvalidOperation
import json
import math
import os
from pathlib import Path
import shutil
import signal
import time
from zoneinfo import ZoneInfo
import serial
from serial.tools import list_ports

PROJECT = Path(__file__).resolve().parents[1]
SOCKET = PROJECT / ".runtime" / "bridge.sock"
TZ = ZoneInfo("Asia/Taipei")


def log(message):
    line = f"{datetime.now(TZ):%H:%M:%S} {message}"
    print(line, flush=True)
    try:
        path = PROJECT / ".runtime" / "bridge.log"
        path.parent.mkdir(exist_ok=True)
        if path.exists() and path.stat().st_size > 1024 * 1024:
            path.write_text("")
        with path.open("a") as output:
            output.write(line + "\n")
    except OSError:
        pass


def display_text(value, limit=24):
    return "".join(c if 32 <= ord(c) <= 126 else "?" for c in str(value))[:limit]


def normalize_window(value):
    if not isinstance(value, dict):
        return None
    used, minutes, reset = (value.get(k) for k in ("usedPercent", "windowDurationMins", "resetsAt"))
    if any(isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x)
           for x in (used, minutes, reset)):
        return None
    if not 1 <= minutes <= 525600 or not 0 <= reset <= 4102444800:
        return None
    return {"used": min(100, max(0, used)), "minutes": int(minutes), "reset": int(reset),
            "reset_local": datetime.fromtimestamp(reset, TZ).strftime("%m/%d %H:%M UTC+8")}


def credit_text(value):
    if not isinstance(value, dict):
        return "--"
    if value.get("unlimited"):
        return "Unlimited"
    if value.get("balance") is None:
        return "--"
    try:
        amount = Decimal(str(value["balance"]))
        return display_text(f"{amount:,.1f}", 16) if amount.is_finite() else "--"
    except (InvalidOperation, ValueError):
        return "--"


def normalize_buckets(result):
    buckets = result.get("rateLimitsByLimitId")
    if not isinstance(buckets, dict) or not buckets:
        legacy = result.get("rateLimits")
        buckets = {legacy.get("limitId") or "codex": legacy} if isinstance(legacy, dict) else {}
    resets = (result.get("rateLimitResetCredits") or {}).get("availableCount")
    resets = resets if isinstance(resets, int) and not isinstance(resets, bool) and resets >= 0 else None
    output = {}
    for key, value in buckets.items():
        if isinstance(value, dict):
            output[key] = {"bucket": display_text(value.get("limitName") or key),
                           "plan": display_text(value.get("planType") or "unknown", 16),
                           "primary": normalize_window(value.get("primary")),
                           "secondary": normalize_window(value.get("secondary")),
                           "credits": credit_text(value.get("credits")), "reset_credits": resets}
    if not output:
        output["codex"] = {"bucket": "codex", "plan": "unknown", "primary": None,
                           "secondary": None, "credits": "--", "reset_credits": resets}
    return output


def resolve_codex(explicit=None):
    candidates = [explicit, os.environ.get("CODEX_BIN"),
                  "/Applications/ChatGPT.app/Contents/Resources/codex-cli/bin/codex",
                  "/Applications/Codex.app/Contents/Resources/codex", shutil.which("codex")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise RuntimeError("Codex CLI not found; set CODEX_BIN")


class CodexClient:
    def __init__(self, binary):
        self.binary, self.process, self.reader = binary, None, None
        self.pending, self.sequence = {}, 0

    async def start(self):
        await self.close()
        self.process = await asyncio.create_subprocess_exec(
            self.binary, "app-server", "--listen", "stdio://",
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL)
        self.reader = asyncio.create_task(self.read_messages())
        await self.request("initialize", {"clientInfo": {
            "name": "esp32_quota_display", "title": "ESP32 Quota Display", "version": "2.0.0"}})
        await self.send({"method": "initialized", "params": {}})

    async def send(self, message):
        self.process.stdin.write((json.dumps(message) + "\n").encode())
        await self.process.stdin.drain()

    async def read_messages(self):
        try:
            while line := await self.process.stdout.readline():
                message = json.loads(line)
                future = self.pending.get(message.get("id"))
                if future is not None and not future.done():
                    if "error" in message:
                        future.set_exception(RuntimeError("Codex quota RPC failed; check login/network"))
                    else:
                        future.set_result(message.get("result", {}))
                elif "id" in message and "method" in message:
                    await self.send({"id": message["id"], "error": {
                        "code": -32601, "message": "Read-only quota client"}})
        except (ValueError, OSError):
            pass
        finally:
            for future in self.pending.values():
                if not future.done():
                    future.set_exception(RuntimeError("Codex app-server disconnected"))

    async def request(self, method, params=None):
        self.sequence += 1
        request_id = self.sequence
        future = asyncio.get_running_loop().create_future()
        self.pending[request_id] = future
        message = {"id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        try:
            await self.send(message)
            return await asyncio.wait_for(future, timeout=20)
        finally:
            self.pending.pop(request_id, None)

    async def quota(self):
        if self.process is None or self.process.returncode is not None:
            await self.start()
        return await self.request("account/rateLimits/read", {"excludeResetCreditDetails": True})

    async def close(self):
        if self.process and self.process.returncode is None:
            self.process.terminate()
            with suppress(asyncio.TimeoutError):
                await asyncio.wait_for(self.process.wait(), 3)
            if self.process.returncode is None:
                self.process.kill()
                await self.process.wait()
        if self.reader:
            self.reader.cancel()
            with suppress(asyncio.CancelledError):
                await self.reader
        self.reader = self.process = None


def find_port(explicit=None):
    if explicit:
        return explicit
    ports = [port.device for port in list_ports.comports() if port.vid == 0x303A]
    if len(ports) != 1:
        raise RuntimeError("Connect one Espressif device or specify --port")
    return ports[0]


class Bridge:
    def __init__(self, args):
        self.args, self.client = args, CodexClient(resolve_codex(args.codex))
        self.stopping, self.refresh = asyncio.Event(), asyncio.Event()
        self.port, self.server = None, None
        self.buckets, self.selected, self.fetched_at, self.seq = {}, "codex", 0, 0
        self.rx, self.last_sent, self.waiting_ack = bytearray(), 0, None
        self.ack_count = 0

    async def control(self, reader, writer):
        try:
            command = await asyncio.wait_for(reader.readline(), 2)
            if command == b"stop\n":
                self.stopping.set()
                writer.write(b"stopping\n")
            else:
                writer.write(b"quota-bridge\n")
            await writer.drain()
        except (ConnectionError, asyncio.TimeoutError):
            pass
        finally:
            writer.close()
            with suppress(ConnectionError):
                await writer.wait_closed()

    def send_snapshot(self):
        if not self.port or not self.buckets:
            return
        now = int(time.time())
        if now - self.fetched_at > 86400:
            return
        self.seq = self.seq % 0xFFFFFFFF + 1
        frame = {"type": "quota", "version": 1, "seq": self.seq, "now": now,
                 "age": max(0, now - self.fetched_at), **self.buckets[self.selected]}
        data = (json.dumps(frame, separators=(",", ":"), allow_nan=False) + "\n").encode()
        if len(data) >= 2048:
            raise RuntimeError("Quota frame exceeds firmware limit")
        self.port.write(data)
        self.last_sent = time.monotonic()
        self.waiting_ack = self.seq

    async def poll(self):
        next_poll = 0
        while not self.stopping.is_set():
            now = time.monotonic()
            if now >= next_poll or self.refresh.is_set():
                self.refresh.clear()
                try:
                    result = await self.client.quota()
                    self.buckets, self.fetched_at = normalize_buckets(result), int(time.time())
                    if self.selected not in self.buckets:
                        self.selected = "codex" if "codex" in self.buckets else next(iter(self.buckets))
                    primary = self.buckets[self.selected]["primary"]
                    detail = f"remaining={100-primary['used']:g}%" if primary else "quota=unavailable"
                    log(f"FETCH_OK bucket={display_text(self.selected)} {detail}")
                    next_poll = time.monotonic() + self.args.interval
                    self.last_sent = 0
                except (RuntimeError, OSError, asyncio.TimeoutError):
                    log("FETCH_FAILED: check Codex login/network; existing data will become STALE")
                    await self.client.close()
                    next_poll = time.monotonic() + min(30, self.args.interval)
                await asyncio.sleep(5)
            await asyncio.sleep(0.2)

    async def serial_loop(self):
        reconnect_at = 0
        while not self.stopping.is_set():
            try:
                if not self.port:
                    if time.monotonic() < reconnect_at:
                        await asyncio.sleep(0.2)
                        continue
                    device = find_port(self.args.port)
                    port = serial.Serial(port=None, baudrate=115200, timeout=0, write_timeout=1)
                    port.dtr = False
                    port.rts = False
                    port.port = device
                    try:
                        port.open()
                    except Exception:
                        port.close()
                        raise
                    self.port, self.rx, self.last_sent = port, bytearray(), 0
                    log(f"USB_CONNECTED {device}")
                self.rx.extend(self.port.read(1024))
                while b"\n" in self.rx:
                    line, _, remainder = self.rx.partition(b"\n")
                    self.rx = bytearray(remainder)
                    text = line.decode("utf-8", errors="replace").strip()
                    if text == "@CODEX_REFRESH":
                        log("TOUCH_REFRESH")
                        self.refresh.set()
                    elif text == "@CODEX_NEXT":
                        keys = list(self.buckets)
                        if keys:
                            self.selected = keys[(keys.index(self.selected) + 1) % len(keys)]
                            self.last_sent = 0
                        log(f"TOUCH_NEXT bucket={display_text(self.selected)}")
                    elif text.startswith("@CODEX_ACK "):
                        try:
                            received = int(text.split()[-1])
                        except ValueError:
                            continue
                        if received == self.waiting_ack:
                            self.waiting_ack = None
                            self.ack_count += 1
                            log(f"DEVICE_ACK seq={received} count={self.ack_count}")
                    elif "codex_demo:" in text or any(x in text for x in ("Guru Meditation", "abort()", "Rebooting")):
                        log(f"DEVICE {text}")
                if len(self.rx) > 8192:
                    self.rx.clear()
                if self.buckets and (self.last_sent == 0 or
                        (self.waiting_ack is not None and time.monotonic() - self.last_sent > 5)):
                    self.send_snapshot()
            except (serial.SerialException, OSError, RuntimeError):
                log("USB_WAITING: reconnect device; close other serial monitors")
                if self.port:
                    self.port.close()
                self.port, self.rx = None, bytearray()
                reconnect_at = time.monotonic() + 3
            await asyncio.sleep(0.05)

    async def run(self):
        SOCKET.parent.mkdir(exist_ok=True)
        if SOCKET.exists():
            try:
                reader, writer = await asyncio.open_unix_connection(str(SOCKET))
            except (ConnectionRefusedError, FileNotFoundError):
                SOCKET.unlink(missing_ok=True)
            else:
                writer.close()
                await writer.wait_closed()
                log("ALREADY_RUNNING: use 06 Stop Quota to stop the active bridge")
                return
        self.server = await asyncio.start_unix_server(self.control, path=str(SOCKET))
        os.chmod(SOCKET, 0o600)
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, self.stopping.set)
        tasks = [asyncio.create_task(self.poll()), asyncio.create_task(self.serial_loop())]
        stop_task = asyncio.create_task(self.stopping.wait())
        try:
            finished, _ = await asyncio.wait(tasks + [stop_task], timeout=self.args.duration or None,
                                             return_when=asyncio.FIRST_COMPLETED)
            for task in finished:
                task.result()
        finally:
            self.stopping.set()
            stop_task.cancel()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, stop_task, return_exceptions=True)
            await self.client.close()
            if self.port:
                self.port.close()
            self.server.close()
            await self.server.wait_closed()
            SOCKET.unlink(missing_ok=True)
            log(f"STOPPED device_ack_count={self.ack_count}")


async def stop_bridge():
    try:
        reader, writer = await asyncio.open_unix_connection(str(SOCKET))
    except (FileNotFoundError, ConnectionRefusedError):
        return
    writer.write(b"stop\n")
    await writer.drain()
    await asyncio.wait_for(reader.readline(), 3)
    writer.close()
    await writer.wait_closed()
    for _ in range(50):
        if not SOCKET.exists():
            return
        await asyncio.sleep(0.1)
    raise RuntimeError("Bridge has not released USB yet")


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--codex")
    parser.add_argument("--interval", type=int, default=60)
    parser.add_argument("--duration", type=int, default=0)
    parser.add_argument("--read-once", action="store_true")
    parser.add_argument("--stop", action="store_true")
    args = parser.parse_args()
    if args.stop:
        await stop_bridge()
        return
    if args.interval < 10:
        parser.error("--interval must be at least 10 seconds")
    if args.read_once:
        client = CodexClient(resolve_codex(args.codex))
        try:
            print(json.dumps(normalize_buckets(await client.quota()), ensure_ascii=False, indent=2))
        finally:
            await client.close()
        return
    await Bridge(args).run()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (RuntimeError, OSError, asyncio.TimeoutError) as error:
        log(f"Bridge stopped: {type(error).__name__}. Check USB, Codex login, and network.")
        raise SystemExit(1)
