# PicPak Firmware

My own firmware for the PicPak, a 4.2" four-colour e-ink frame built on an ESP32-C3. The stock firmware stays in `backup/` so I can always go back.

The frame has five modes, stepped through with its one button. Presses made during a refresh still count, so two quick presses move two modes on, and it wraps from the last back to the first. Everything is drawn in the visual language of Ron Cobb's semiotic standard for *Alien*: square hazard placards, crisp solid colours, readings framed as ship's instruments.

| Mode | What it shows |
|---|---|
| 1 Environmental panel | Cary's weather for the next twelve hours as readings and hazard placards |
| 2 System Updates | NPR headlines, each with an orbit placard that shows its age |
| 3 System Status | The board itself: battery and its trend, feeds, Wi-Fi, chip temperature, storage, firmware, attitude |
| 4 Orbital Tracking | The next visible ISS pass on a sky scope, the nearest asteroid this week, sunrise, sunset and moon phase |
| 5 Crew Manifest | The household as a ship's crew, with status from a daily schedule, duty rotation and a daily Special Order |

The board sleeps between wakes. It wakes hourly to fetch the forecast and headlines, and every six hours the ISS elements and asteroid data, then redraws whatever is on screen. With mode 4 showing it also wakes ten minutes before a visible pass and again just after. Holding the button 3 s enters maintenance mode, which keeps USB up for flashing.

## Decisions

Decided on 2026-09-13:

| Decision | Choice |
|---|---|
| Forecast location | Cary, NC |
| Units | Imperial (°F, mph, inches, miles) with a 24-hour clock |
| Mode indicator | Numbers in the footer, the current one inverted |
| Lookahead | Hours 1 to 6 as the main row, hours 7 to 12 as the small strip |
| Weather source | Open-Meteo hourly forecast, no key, requested in °F, mph and inches (visibility comes back in feet) |
| Placard frame | Rounded corners |
| Placards | A for everything except thermal high (B, rising chevrons), precipitation (B, three drops) and visibility (B, veiled beacon) |
| Headlines | NPR's top stories RSS, list layout |
| System Status | Console layout; clock drift and counters left off |
| Orbital Tracking | Sky scope layout; visible passes only; sun and moon on. ISS elements from CelesTrak with SGP4 on the board, asteroids from JPL's close-approach API, both without keys |
| Crew Manifest | Roster layout, duty rotation and hypersleep on, 366 Special Orders compiled in. Names and schedules live in the git-ignored `crew_config.h` |
| Fonts | Pixel fonts at their native grid (Jersey 10, Silkscreen) wherever text is small; Big Shoulders only for large numerals |

Design reviews, with picks saved on each page: the environmental panel ([artifact](https://claude.ai/code/artifact/479ad10c-99f9-4e4b-addf-b098153fff5f)), placard alternatives ([artifact](https://claude.ai/code/artifact/945aba5d-2429-48dc-9f4d-5d63adb6f045)), System Updates ([artifact](https://claude.ai/code/artifact/7efda3f7-670f-4974-b792-129e1f5e41de)), System Status ([artifact](https://claude.ai/code/artifact/cdabb0dd-f9cf-43c5-951b-29fe3d1f186e)), and Orbit and Crew ([artifact](https://claude.ai/code/artifact/418d9918-e4d0-401e-b55a-4e3cdb419b52)).

## Setup

`firmware/main/secrets.h` holds the Wi-Fi credentials and `firmware/main/crew_config.h` the crew; git ignores both. Copy `secrets.example.h` and `crew_config.example.h` to start. Without `crew_config.h` the build uses the example crew.

## Commands

| Command | What it does |
|---|---|
| `make build` | Build with ESP-IDF v5.5.5 from `~/esp/esp-idf-v5.5.5` |
| `make flash` | Build and flash over USB; the board must be awake |
| `python tools/flash_on_wake.py 60` | Wait up to 60 minutes for the board to wake, then flash it (run from the IDF environment) |
| `make log DURATION=30` | Print the console for 30 s without resetting the board, following it through sleep, with host timestamps |
| `make monitor` | Interactive console, needs a real terminal |
| `make test` | Host tests: framebuffer, wake classification, forecast, drawing, scheduling, headlines, status, orbits against Skyfield, crew |
| `make preview MODE=4` | Render a screen on the Mac to `build/preview.png`; `PREVIEW_NOW=` picks the time |
| `make assets` | Redraw placards and fonts from `tools/assets/rasterize.html` into `assets_gen.c` |
| `make orders` | Check `firmware/orders.txt` and regenerate `orders_gen.c` |
| `make restore` | Write this unit's stock backup back, 5 to 10 minutes |

The USB port only exists while the chip is awake. To flash, hold the button 3 s for maintenance mode, or leave `flash_on_wake.py` waiting for the next wake.

## Hardware

| Part | Detail |
|---|---|
| SoC | ESP32-C3 rev v0.4, single-core RISC-V, 400 KB RAM, no PSRAM |
| Flash | Macronix, reports 32 MB, the C3 only addresses the lower 16 MB |
| Panel | 4.2" 400x300, black/white/yellow/red, UC81xx-class, full refresh 16.2 s |
| IMU | ST LSM6DS3TR-C on SPI, WHO_AM_I 0x6A |
| Radios | 2.4 GHz Wi-Fi, BLE 5 |
| Input | One button, one LED |
| Power | About 550 mAh Li-Po, USB-C charging, no PMIC, no charger detect |

| GPIO | Use |
|---|---|
| 6, 3, 4 | SPI SCLK, MOSI, MISO (panel and IMU share the bus) |
| 9, 8, 10, 20 | Panel CS, DC, RST, BUSY (BUSY is low while busy) |
| 7, 5 | IMU CS, and the IMU's INT1 output (GPIO5 can wake the C3 from deep sleep) |
| 2 | Button to ground, and the battery ADC through a x1.45 divider |
| 21 | LED, active-low, also UART0 TX |
| 18, 19 | Native USB serial/JTAG |

The pin map and panel register values come from [varanu5/picpak-tesserae-client](https://github.com/varanu5/picpak-tesserae-client), which reverse-engineered the same hardware. The IMU model and stock behaviour come from strings in my unit's stock image. On my unit I have confirmed the chip, flash and eFuses, the panel pins and register setup, the IMU chip select, ID and INT1 wake line, the battery ADC, and the button as a deep-sleep wake source. The LED, the battery capacity, the missing charger detect and the sleep current are still unverified.

Pixels are 2 bits each in 100-byte rows, with `0` black, `1` white, `2` yellow and `3` red. The panel scans rows bottom to top, and `fb_panel_row` handles that.

## Stock Firmware

My unit shipped with `simplestick` V0.3.2, built on 2 April 2026 with ESP-IDF v5.5. It runs over BLE with an AT-style command set (`AT+MAC?`, `AT+VOLTAGE?`, `AT+IMU=`, `AT+SN?`) and appears to have a fridge mode (`icebox_door_switch`, "Door action angle_x") that uses the IMU to catch a door opening and show the next photo.

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 256 KB, holds this unit's serial and radio calibration |
| otadata | 0x49000 | 8 KB |
| phy_init | 0x4b000 | 4 KB |
| ota_0 | 0x50000 | 1.5 MB, the stock app |
| ota_1 | 0x1d0000 | 1.5 MB, empty |
| storage | 0x350000 | 12 MB, photos |

`backup/` holds two identical reads of the lower 16 MB (sha256 `c0d7cd7b…a059`) and the eFuse summary. Secure boot and flash encryption are off, and none of the eFuses are burned. The backup is specific to this unit, so it is never committed.

## Rules That Protect The Board

The first four come from the varanu5 project's testing and I have not reproduced them. They are cheap to follow and expensive to learn.

- Never run `erase_flash`. A full erase of this flash chip fails partway.
- Never write above 16 MB. Addresses there wrap around onto the bootloader.
- Large reads and writes go through the ROM loader with `--no-stub`. The stub has corrupted big transfers on this chip. Normal app flashes are small and verify their hash.
- The supply sags under load. Keep Wi-Fi transmit power at 10 dBm or lower, and turn the radio off before a panel refresh.
- Opening the USB serial port resets the C3 unless RTS is released before DTR. I hit this, and `tools/log.py` now releases them in the right order.
