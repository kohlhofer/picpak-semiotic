# SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Back up a PicPak's flash before replacing its firmware, or show what chip it is.

usage (from the ESP-IDF environment):
  python tools/backup.py            read the lower 16 MB twice into backup/ and compare
  python tools/backup.py --info     chip, flash and eFuse summary only; writes nothing

The backup is two full reads through the ROM loader (--no-stub; the stub has
corrupted large transfers on this board), chained so the chip never boots in
between. They must match. It also saves the eFuse summary. The image belongs to
this one unit: its settings region holds the serial number and radio
calibration. Keep it; never commit it. Each read takes about 10 minutes.

Options for testing the tool: --size 0x10000 --dir build/backup-test
"""
import argparse
import hashlib
import io
import pathlib
import sys
import contextlib

import espefuse
import esptool

import picpak_usb

ROOT = pathlib.Path(__file__).resolve().parents[1]

p = argparse.ArgumentParser()
p.add_argument("--info", action="store_true")
p.add_argument("--size", default="0x1000000")
p.add_argument("--dir", default=str(ROOT / "backup"))
p.add_argument("--minutes", type=float, default=10)
args = p.parse_args()
out = pathlib.Path(args.dir)
size = int(args.size, 0)
if size > 0x1000000:
    sys.exit("Refusing to read above 16 MB: the C3 cannot address it and the upper half mirrors the lower.")

images = [out / "stock_16mb_1.bin", out / "stock_16mb_2.bin"]
if not args.info and any(f.exists() for f in images):
    sys.exit(f"{out} already holds a backup. Move it somewhere safe first; a second run "
             "would read whatever firmware is on the board now, not the stock one.")


def esptool_run(argv):
    esptool.main(argv)


def work(port):
    if args.info:
        esptool_run(["--chip", "esp32c3", "-p", port, "--before", "no_reset", "--after", "no_reset", "flash_id"])
        espefuse.main(["--chip", "esp32c3", "-p", port, "--before", "no_reset", "summary"])
        return
    out.mkdir(parents=True, exist_ok=True)
    for i, image in enumerate(images):
        picpak_usb.say(f"read {i + 1} of 2 into {image}")
        esptool_run(["--chip", "esp32c3", "-p", port, "--no-stub", "--before", "no_reset", "--after", "no_reset",
                     "read_flash", "0x0", hex(size), str(image)])
    summary = io.StringIO()
    with contextlib.redirect_stdout(summary):
        espefuse.main(["--chip", "esp32c3", "-p", port, "--before", "no_reset", "summary"])
    (out / "efuse_summary.txt").write_text(summary.getvalue())


if not picpak_usb.run_in_loader(args.minutes, work, "board info" if args.info else "backup"):
    sys.exit(1)
if args.info:
    sys.exit(0)

sums = [hashlib.sha256(f.read_bytes()).hexdigest() for f in images]
(out / "SHA256SUMS").write_text("".join(f"{s}  {f.name}\n" for s, f in zip(sums, images)))
if sums[0] != sums[1]:
    sys.exit(f"The two reads differ ({sums[0][:12]} and {sums[1][:12]}). Do not flash. Unplug, "
             "move these files aside and run the backup again.")
picpak_usb.say(f"backup verified: both reads are {sums[0]}")
summary = (out / "efuse_summary.txt").read_text()
print("Saved the eFuse summary. Check that secure boot and flash encryption are off before flashing:"
      "\n  SECURE_BOOT_EN should be False, SPI_BOOT_CRYPT_CNT should be 0.")
