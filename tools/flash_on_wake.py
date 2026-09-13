"""Flash a sleeping PicPak by catching one of its short wakes.

usage (from the IDF environment): python tools/flash_on_wake.py [MINUTES]

A timer wake keeps the USB port up for about a second, which is less than it
takes to start esptool. This keeps esptool imported and polls for the port,
so the reset into the ROM loader goes out within a few milliseconds. Once in
the loader the chip cannot sleep, so the write itself has all the time it needs.
"""
import glob
import os
import pathlib
import sys
import time

import esptool
import serial

minutes = float(sys.argv[1]) if len(sys.argv) > 1 else 15
build = pathlib.Path(__file__).resolve().parents[1] / "firmware/build"
os.chdir(build)

deadline = time.time() + minutes * 60
attempts = 0
while time.time() < deadline:
    ports = glob.glob("/dev/cu.usbmodem*")
    if not ports:
        time.sleep(0.01)
        continue
    attempts += 1
    print(f"{time.strftime('%H:%M:%S')} port {ports[0]}, attempt {attempts}", flush=True)
    try:
        # esptool's own reset sleeps 100 ms per step, longer than a timer wake
        # lasts. Send the same USB-Serial/JTAG sequence with no pauses: set the
        # download flag (RTS off, DTR on), reset (RTS on, DTR off), release.
        # In the ROM loader the chip stays awake for esptool to connect.
        # Leave with a watchdog reset: the USB bridge's reset is a core reset
        # that keeps the download flag, so --after hard_reset lands straight
        # back in the loader (esptool's troubleshooting page says as much).
        s = serial.Serial()
        s.port, s.dtr, s.rts = ports[0], True, True
        s.open()
        s.rts = False
        s.rts = True
        s.dtr = False
        s.rts = True
        time.sleep(0.01)
        s.dtr = False
        s.rts = False
        s.close()
        time.sleep(0.4)
        esptool.main(["--chip", "esp32c3", "-p", ports[0], "-b", "460800",
                      "--before", "no_reset", "--after", "watchdog_reset",
                      "write_flash", "@flash_args"])
    except (Exception, SystemExit) as e:
        print(f"  missed: {type(e).__name__}: {e}", flush=True)
        # Wait for this wake's port to go away before looking again.
        while glob.glob("/dev/cu.usbmodem*") and time.time() < deadline:
            time.sleep(0.05)
        continue
    # Outside the try: esptool signals success by returning, and a sys.exit
    # inside it would be caught above and flash the board again.
    print(f"{time.strftime('%H:%M:%S')} flashed", flush=True)
    sys.exit(0)
print("gave up", flush=True)
sys.exit(1)
