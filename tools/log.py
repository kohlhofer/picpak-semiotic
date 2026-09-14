# SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Print the PicPak's USB console without resetting it, following it through sleep.

usage: log.py PORT_GLOB [SECONDS]

Deep sleep powers down the C3's USB, so the port disappears. This waits for it
to come back and reattaches. Every line gets a host timestamp, which is the
reference for measuring how far the board's sleep timer drifts.
"""
import glob
import sys
import time
from datetime import datetime

import serial

pattern = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30


def say(text):
    print(f"{datetime.now():%H:%M:%S.%f}"[:-3], text, flush=True)


def attach(path):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = path, 115200, 0.1
    # The C3's USB bridge resets the chip when RTS is asserted while DTR is not.
    # Open with both asserted, then release RTS before DTR, the order
    # esp-idf-monitor uses for --no-reset. Releasing DTR first resets the board.
    s.dtr = s.rts = True
    s.open()
    s.rts = False
    s.dtr = s.dtr  # re-send DTR so the RTS change reaches the device
    s.dtr = False
    return s


end = time.time() + seconds
port, buf = None, b""
while time.time() < end:
    if port is None:
        paths = glob.glob(pattern)
        if not paths:
            time.sleep(0.05)
            continue
        try:
            port = attach(paths[0])
            say("[log] attached")
        except (serial.SerialException, OSError):
            time.sleep(0.1)
        continue
    try:
        buf += port.read(4096)
    except (serial.SerialException, OSError):
        say("[log] detached (sleep or reset)")
        port, buf = None, b""
        continue
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        say(line.decode(errors="replace").rstrip("\r"))
