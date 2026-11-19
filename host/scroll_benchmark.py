#!/usr/bin/env python3
"""Measure completed display frames without changing persisted device settings."""
import argparse
import json
import re
import statistics
import time
from pathlib import Path
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/cu.usbmodem101')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    transcript = bytearray()
    runs = []
    interruptions = []
    with serial.Serial(args.port, 115200, timeout=0.02) as connection:
        def drain(seconds):
            block = bytearray()
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                data = connection.read(connection.in_waiting or 1)
                block.extend(data)
                transcript.extend(data)
            text = block.decode('utf-8', errors='replace')
            print(text, end='', flush=True)
            return text

        def command(value, seconds):
            connection.write(('@UI ' + value + '\n').encode())
            return drain(seconds)

        command('wake', 0.5)
        # Let alerts/toasts and any preceding page entrance finish before measuring.
        command('page 0', 4.0)
        for index in range(args.runs):
            for scene, operation in [('carousel', 'bench scroll'), ('list', 'bench list')]:
                block = command(operation, 5.4)
                if 'UI_CLICK' in block:
                    interruptions.append({'run': index + 1, 'scene': scene, 'reason': 'Operator interaction during programmed trajectory'})
                match = re.search(r'PRESENT name=' + scene + r' completed_frames=(\d+) fps=([\d.]+)', block)
                if not match:
                    raise RuntimeError('Missing completed-frame sample: ' + scene)
                runs.append({'run': index + 1, 'scene': scene, 'frames': int(match[1]), 'fps': float(match[2])})
        command('state', 0.5)
        command('page 0', 0.4)
    text = transcript.decode('utf-8', errors='replace')
    (out / 'performance.log').write_text(text)
    failures = re.findall(r'Guru Meditation|assert failed|CORRUPT HEAP|Backtrace:|DMA completion timeout|Display transfer failed', text)
    errors = re.findall(r'timeouts=(\d+) dma_errors=(\d+)', text)
    result = {
        'pass': not failures and not interruptions and all(a == '0' and b == '0' for a, b in errors),
        'runs': runs,
        'median_fps': {s: statistics.median(r['fps'] for r in runs if r['scene'] == s) for s in ['carousel', 'list']},
        'errors': failures,
        'operator_interruptions': interruptions,
        'method': 'USB-connected physical device; identical 4.8 s programmed trajectories, completed display frames. Not human touch latency or optical tearing validation.'
    }
    (out / 'performance.json').write_text(json.dumps(result, indent=2) + '\n')
    if not result['pass']:
        raise SystemExit('SCROLL_BENCHMARK_FAIL')
    print('SCROLL_BENCHMARK_OK ' + json.dumps(result['median_fps']))


if __name__ == '__main__':
    main()
