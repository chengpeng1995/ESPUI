#!/usr/bin/env python3
"""Recover missing sources at their immutable commits; never reset an existing checkout."""
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for item in json.loads((root / "upstream-lock.json").read_text()):
    destination = root / item["path"]
    if item["path"] == "vendor/waveshare-reference":
        continue  # The small reference extracts and their licenses are delivered in this project.
    if destination.exists():
        actual = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
        if actual != item["commit"]:
            raise SystemExit(f"Version differs in {destination}; preserve local work and resolve manually")
        continue
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "clone", "--no-checkout", "https://github.com/" + item["repository"] + ".git", str(destination)], check=True)
    subprocess.run(["git", "-C", str(destination), "checkout", "--detach", item["commit"]], check=True)
print("Pinned Git dependencies verified")
