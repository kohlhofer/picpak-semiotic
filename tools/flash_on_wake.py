# SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Flash the PicPak firmware, catching the board whenever it is awake.

usage (from the ESP-IDF environment): python tools/flash_on_wake.py [MINUTES]

Run `idf.py build` (or `make build`) first. Works on a board running the stock
firmware (hold the button while plugging in so it stays awake) and on one
running this firmware (it wakes on its own, or hold the button 3 s).
"""
import os
import pathlib
import sys

import esptool

import picpak_usb

minutes = float(sys.argv[1]) if len(sys.argv) > 1 else 15
build = pathlib.Path(__file__).resolve().parents[1] / "firmware/build"
if not (build / "flash_args").exists():
    sys.exit("No firmware/build/flash_args: run `make build` first.")


def flash(port):
    # Leave with a watchdog reset: the USB bridge's reset is a core reset that
    # keeps the download flag, so --after hard_reset lands back in the loader.
    esptool.main(["--chip", "esp32c3", "-p", port, "-b", "460800", "--before", "no_reset",
                  "--after", "watchdog_reset", "write_flash", "@" + str(build / "flash_args")])


os.chdir(build)   # flash_args names the images relative to the build folder
ok = picpak_usb.run_in_loader(minutes, flash, "flashing")
if ok:
    picpak_usb.say("flashed")
sys.exit(0 if ok else 1)
