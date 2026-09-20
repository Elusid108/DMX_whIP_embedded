"""Read C5 UART and append NDJSON to debug-981ddd.log."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[1]
LOG = ROOT / "debug-981ddd.log"


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM7")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--seconds", type=float, default=20)
    args = p.parse_args()
    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    deadline = time.time() + args.seconds
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").strip()
        if not line:
            continue
        rec = {
            "sessionId": "981ddd",
            "timestamp": int(time.time() * 1000),
            "location": "dbg981-ingest.py",
            "message": line,
            "data": {"port": args.port},
            "runId": "led-dark",
        }
        if line.startswith("[dbg981]"):
            rec["hypothesisId"] = "serial"
        LOG.open("a", encoding="utf-8").write(json.dumps(rec) + "\n")
        print(line)
    ser.close()


if __name__ == "__main__":
    main()
