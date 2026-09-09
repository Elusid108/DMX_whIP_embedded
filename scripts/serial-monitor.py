"""USB serial viewer for the Waveshare ESP32-S3-Matrix (COM3, 115200)."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200
WINDOW_TITLE = "ESP32 COM3"


def find_port(preferred: str) -> str:
    ports = list(list_ports.comports())
    for p in ports:
        if p.device.upper() == preferred.upper():
            return p.device
    for p in ports:
        if "303A" in (p.hwid or "").upper():
            return p.device
    raise SystemExit("No Espressif COM port found")


def wait_for_port(preferred: str, timeout_s: float = 20.0) -> str:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            return find_port(preferred)
        except SystemExit:
            time.sleep(0.25)
    raise serial.SerialException(f"{preferred} did not reappear")


def open_serial(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.open()
    ser.setDTR(True)
    ser.setRTS(False)
    return ser


def poke_cdc(ser: serial.Serial) -> None:
    """ESP32 HWCDC only sets Serial=connected after it receives a host byte."""
    try:
        ser.write(b"\n")
        ser.flush()
    except serial.SerialException as exc:
        print(f"CDC poke failed: {exc}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    try:
        port = find_port(args.port)
    except SystemExit as exc:
        print(exc)
        return 1

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("", encoding="utf-8")

    print(f"{WINDOW_TITLE}  {port}  {args.baud}")
    print(f"log: {log_path}")
    print("Close this window to release the port.\n")

    try:
        ser = open_serial(port, args.baud)
        poke_cdc(ser)
    except serial.SerialException as exc:
        print(f"Failed to open {port}: {exc}")
        return 1

    with log_path.open("a", encoding="utf-8", newline="") as log:
        while True:
            try:
                chunk = ser.read(256)
            except serial.SerialException as exc:
                print(f"\nUSB serial dropped ({exc}). Waiting for {args.port}...")
                try:
                    ser.close()
                except Exception:
                    pass
                try:
                    port = wait_for_port(args.port)
                    ser = open_serial(port, args.baud)
                    poke_cdc(ser)
                    print(f"Reconnected on {port}. Waiting for buffered boot log...\n")
                except serial.SerialException as retry_exc:
                    print(f"Reconnect failed: {retry_exc}")
                    return 1
                continue
            if not chunk:
                continue
            text = chunk.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            log.write(text)
            log.flush()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
