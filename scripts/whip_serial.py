"""Send one serial command to a whIP node and print its @whip reply.

  python scripts/whip_serial.py ports
  python scripts/whip_serial.py COM11 id
  python scripts/whip_serial.py COM11 get
  python scripts/whip_serial.py COM11 test rainbow
  python scripts/whip_serial.py COM11 set '{"name":"Stage-01","bri":20}'

Needs pyserial; without it the script re-runs itself under PlatformIO's
Python. Log lines are skipped; only the line starting "@whip " is printed.

DTR: an RP2040 / RP2350 only sends on its USB serial while DTR is raised, so
it is raised for those. For every other port DTR and RTS stay low, so an
ESP32 board behind a USB-UART bridge does not reset when the port opens.
"""
import os
import subprocess
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    # This Python has no pyserial: run again under PlatformIO's, which does.
    penv = os.path.join(os.path.expanduser("~"), ".platformio", "penv")
    pio_python = os.path.join(penv, "Scripts", "python.exe") if os.name == "nt" \
        else os.path.join(penv, "bin", "python")
    if os.path.isfile(pio_python) and os.path.abspath(sys.executable) != os.path.abspath(pio_python):
        sys.exit(subprocess.call([pio_python, os.path.abspath(__file__)] + sys.argv[1:]))
    sys.exit("pyserial is missing. Install it with: python -m pip install pyserial")

# Raspberry Pi, Seeed (XIAO RP2040 / RP2350 running firmware).
RP_VIDS = (0x2E8A, 0x2886)
KINDS = {0x303A: "ESP32 (native USB)", 0x10C4: "USB-UART bridge", 0x1A86: "USB-UART bridge",
         0x2E8A: "RP2040 / RP2350", 0x2886: "Seeed board"}


def print_ports():
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        print("no serial ports found")
        return
    for p in ports:
        ids = "%04X:%04X" % (p.vid, p.pid) if p.vid is not None else "----:----"
        print("%-8s %s  %s" % (p.device, ids, KINDS.get(p.vid, p.description or "")))


def main():
    if len(sys.argv) == 2 and sys.argv[1].lower() in ("ports", "list"):
        print_ports()
        return 0
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    port, cmd = sys.argv[1], " ".join(sys.argv[2:])
    info = next((p for p in list_ports.comports() if p.device.lower() == port.lower()), None)
    if info is None:
        print("%s is not there. Ports on this PC:" % port)
        print_ports()
        return 1
    s = serial.Serial()
    s.port = info.device
    s.baudrate = 115200
    s.timeout = 0.1
    s.dtr = info.vid in RP_VIDS
    s.rts = False
    try:
        s.open()
    except serial.SerialException:
        print("%s is busy. Close the companion app or any serial monitor using it." % port)
        return 1
    try:
        s.reset_input_buffer()
        s.write(("whip " + cmd + "\n").encode())
        buf = b""
        end = time.time() + 3
        while time.time() < end:
            buf += s.read(4096)
            # Whole lines only: the reply may arrive in pieces.
            for line in buf.split(b"\n")[:-1]:
                line = line.strip()
                if line.startswith(b"@whip "):
                    print(line[6:].decode("utf-8", "replace"))
                    return 0
        print("no @whip reply (firmware older than 0.53, or the board is in its bootloader)")
        return 1
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
