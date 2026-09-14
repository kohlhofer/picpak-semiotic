"""Finding the PicPak on USB and getting it into the ESP32-C3's ROM loader.

The board sleeps most of the time, and its USB port only exists while it is
awake: about a second on a timer wake, as long as the button is held, or for
as long as maintenance mode lasts. esptool's own reset pauses 100 ms per step,
which can miss a short wake, so this sends the same USB-Serial/JTAG sequence
with no pauses. Once in the loader the chip stays awake until it is reset.
"""
import glob
import os
import sys
import time

import serial


def port_glob():
    if os.environ.get("PORT_GLOB"):
        return os.environ["PORT_GLOB"]
    return "/dev/cu.usbmodem*" if sys.platform == "darwin" else "/dev/ttyACM*"


def ports():
    return sorted(glob.glob(port_glob()))


def wait_for_port(deadline):
    """The first matching port that appears before deadline (time.time()), or None."""
    while time.time() < deadline:
        found = ports()
        if found:
            return found[0]
        time.sleep(0.01)
    return None


def enter_loader(port):
    """Reset the chip into the ROM loader through the USB-Serial/JTAG bridge."""
    # Set the download flag (RTS off, DTR on), reset (RTS on, DTR off), release.
    s = serial.Serial()
    s.port, s.dtr, s.rts = port, True, True
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


def wait_for_gone(deadline):
    while ports() and time.time() < deadline:
        time.sleep(0.05)


def run_in_loader(minutes, action, what):
    """Wait for the board, put it in the loader and run action(port).

    action raises (or esptool exits) on failure; the board's next wake is then
    tried until the time runs out. Returns True once action succeeds.
    """
    deadline = time.time() + minutes * 60
    say(f"waiting up to {minutes:g} min for {port_glob()}: plug the PicPak in, or hold its button")
    attempts = 0
    while time.time() < deadline:
        port = wait_for_port(deadline)
        if not port:
            break
        attempts += 1
        say(f"port {port}, attempt {attempts}: {what}")
        try:
            enter_loader(port)
            action(port)
        except (Exception, SystemExit) as e:
            say(f"  missed: {type(e).__name__}: {e}")
            wait_for_gone(deadline)
            continue
        return True
    say("gave up: the port never stayed long enough")
    return False


def say(text):
    print(f"{time.strftime('%H:%M:%S')} {text}", flush=True)
