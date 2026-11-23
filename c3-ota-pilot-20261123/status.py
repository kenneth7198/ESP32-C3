"""Read the C3 firmware and Wi-Fi status over USB."""
import sys
import time

import serial

from provision import wait_for


if len(sys.argv) != 2:
    raise SystemExit("Usage: python status.py COM14")
with serial.Serial(sys.argv[1], 115200, timeout=1) as port:
    time.sleep(2)
    port.reset_input_buffer()
    port.write(b"\x1bSTATUS\n")
    print(wait_for(port, "STATUS version="))
