#!/usr/bin/env python3
"""Send one command to this project's running simulator, never the USB device."""
import socket
import sys
from pathlib import Path

path = Path(__file__).resolve().parents[1] / ".runtime/simulator.sock"
if len(sys.argv) != 2 or "\n" in sys.argv[1]:
    raise SystemExit("Usage: python3 host/ui_control.py 'ui app 2'")
with socket.socket(socket.AF_UNIX) as client:
    client.settimeout(3)
    client.connect(str(path))
    client.sendall((sys.argv[1] + "\n").encode())
    print(client.recv(1024).decode().strip())
