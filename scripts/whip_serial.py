"""Send one serial command to a whIP node and print its @whip reply.

  python scripts/whip_serial.py COM11 id
  python scripts/whip_serial.py COM11 get
  python scripts/whip_serial.py COM11 set '{"name":"Stage-01","bri":20}'

Uses pyserial (PlatformIO's Python has it). Log lines are skipped; only the
line starting "@whip " is printed. DTR/RTS stay low so a UART-bridge board
does not reset when the port opens.
"""
import sys
import time

import serial


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    port, cmd = sys.argv[1], " ".join(sys.argv[2:])
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.1
    s.dtr = False
    s.rts = False
    s.open()
    try:
        s.reset_input_buffer()
        s.write(("whip " + cmd + "\n").encode())
        buf = b""
        end = time.time() + 3
        while time.time() < end:
            buf += s.read(4096)
            for line in buf.split(b"\n"):
                line = line.strip()
                if line.startswith(b"@whip "):
                    print(line[6:].decode("utf-8", "replace"))
                    return 0
        print("no @whip reply (old firmware, or the port is busy)")
        return 1
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
