# SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write a PicPak's own stock backup back to it.

usage (from the ESP-IDF environment): python tools/restore.py [backup/stock_16mb_1.bin]

Checks the image against backup/SHA256SUMS first and refuses a file that is not
exactly 16 MB. Writes through the ROM loader (--no-stub) and never erases the
whole chip: a full erase is reported to fail partway on this flash. Expect 5 to
10 minutes, starting with a silent erase of the written range that looks stuck.

Untested: this path has not yet been run end to end on a PicPak.
"""
import hashlib
import pathlib
import sys

import esptool

import picpak_usb

ROOT = pathlib.Path(__file__).resolve().parents[1]
image = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "backup/stock_16mb_1.bin"
sums = image.parent / "SHA256SUMS"

if not image.exists():
    sys.exit(f"No {image}. The restore needs this unit's own backup from tools/backup.py.")
data = image.read_bytes()
if len(data) != 0x1000000:
    sys.exit(f"{image} is {len(data)} bytes, not 16 MB. Refusing to write it.")
digest = hashlib.sha256(data).hexdigest()
if not sums.exists() or digest not in sums.read_text():
    sys.exit(f"{image} does not match {sums}. Refusing to write an unverified image.")


def write(port):
    esptool.main(["--chip", "esp32c3", "-p", port, "--no-stub", "--before", "no_reset", "--after", "watchdog_reset",
                  "write_flash", "--flash_size", "16MB", "0x0", str(image)])


ok = picpak_usb.run_in_loader(10, write, "restoring the stock firmware")
if ok:
    picpak_usb.say("restored; the stock firmware should start")
sys.exit(0 if ok else 1)
