# SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
# SPDX-License-Identifier: AGPL-3.0-or-later
SHELL    := /bin/bash
IDF_PATH ?= $(HOME)/esp/esp-idf-v5.5.5
UNAME    := $(shell uname -s)
# The C3's native USB shows up as a CDC-ACM port.
PORT_GLOB ?= $(if $(filter Darwin,$(UNAME)),/dev/cu.usbmodem*,/dev/ttyACM*)
PORT     ?= $(firstword $(wildcard $(PORT_GLOB)))
DURATION ?= 30
WAIT     ?= 15

# ESP-IDF 5.5 wants Python 3.9 to 3.13. A newer system python3 (Homebrew's
# 3.14, say) breaks export.sh, so put the IDF's own environment first when the
# installer made one.
IDF_PY_ENV ?= $(lastword $(sort $(wildcard $(HOME)/.espressif/python_env/idf5.5_py*_env)))
IDF_ENV := $(if $(IDF_PY_ENV),export PATH="$(IDF_PY_ENV)/bin:$$PATH" &&) export IDF_PATH="$(IDF_PATH)" PORT_GLOB='$(PORT_GLOB)' \
	&& source "$(IDF_PATH)/export.sh" >/dev/null
IDF     := $(IDF_ENV) && idf.py -C firmware

.PHONY: config build flash board-info backup restore log monitor test preview screenshots assets orders clean idf-check port

# Start your settings from the example. Never overwrites an existing config.h.
config:
	@if [ -e firmware/main/config.h ]; then echo "firmware/main/config.h already exists; edit it."; \
	else cp firmware/main/config.example.h firmware/main/config.h && echo "Created firmware/main/config.h: set your Wi-Fi and location in it."; fi

build: idf-check
	@test -e firmware/main/config.h || { echo "No firmware/main/config.h yet. Run 'make config' and edit it."; exit 1; }
	$(IDF) build

# Build, then flash as soon as the board is awake: plugged in with the button
# held (stock firmware), on its own next wake, or in maintenance mode.
flash: build
	$(IDF_ENV) && python tools/flash_on_wake.py $(WAIT)

# Chip, flash and eFuse summary. Reads nothing else and writes nothing.
board-info: idf-check
	$(IDF_ENV) && python tools/backup.py --info --minutes $(WAIT)

# Two full reads of the lower 16 MB into backup/, compared. About 20 minutes.
backup: idf-check
	$(IDF_ENV) && python tools/backup.py --minutes $(WAIT)

# Put this unit's own stock backup back. Untested end to end; see the README.
restore: idf-check
	$(IDF_ENV) && python tools/restore.py backup/stock_16mb_1.bin

# Print the console for DURATION seconds without resetting the board, following
# it through deep sleep, with host timestamps.
log: idf-check
	$(IDF_ENV) && python tools/log.py '$(PORT_GLOB)' $(DURATION)

# Interactive console; needs a real terminal and an awake board. Ctrl-] quits.
monitor: idf-check port
	$(IDF) -p $(PORT) monitor

CJSON   := $(IDF_PATH)/components/json/cJSON
HOST_CC := cc -std=gnu11 -Wall -Wextra -Werror -Ifirmware/main -I$(CJSON)
SCREEN  := firmware/main/fb.c firmware/main/gfx.c firmware/main/assets_gen.c
WX      := firmware/main/wx.c $(CJSON)/cJSON.c
FIXTURE := test/fixtures/open-meteo-cary-2026-09-13.json
ORBIT   := firmware/main/sgp4.c firmware/main/sky.c firmware/main/pass.c firmware/main/orbit.c
CREW    := firmware/main/crew.c firmware/main/orders_gen.c
PREVIEW_SRC := $(SCREEN) $(WX) firmware/main/news.c firmware/main/sched.c firmware/main/status.c firmware/main/batt.c \
	$(ORBIT) $(CREW) firmware/main/screen.c tools/preview.c

# Host tests. Needs a C compiler and the ESP-IDF checkout (for cJSON), not the board.
test:
	@test -d "$(CJSON)" || { echo "No cJSON at $(CJSON). Set IDF_PATH to your ESP-IDF checkout."; exit 1; }
	@mkdir -p build
	$(HOST_CC) firmware/main/fb.c test/test_fb.c -o build/test_fb
	$(HOST_CC) firmware/main/wake.c test/test_wake.c -o build/test_wake
	$(HOST_CC) $(WX) test/test_wx.c -lm -o build/test_wx
	$(HOST_CC) $(SCREEN) test/test_gfx.c -o build/test_gfx
	$(HOST_CC) firmware/main/sched.c firmware/main/batt.c test/test_sched_batt.c -o build/test_sched_batt
	$(HOST_CC) $(SCREEN) firmware/main/news.c firmware/main/sched.c test/test_news.c -o build/test_news
	$(HOST_CC) firmware/main/status.c firmware/main/batt.c test/test_status.c -lm -o build/test_status
	$(HOST_CC) $(ORBIT) $(CJSON)/cJSON.c test/test_orbit.c -lm -o build/test_orbit
	$(HOST_CC) $(CREW) $(SCREEN) test/test_crew.c -o build/test_crew
	./build/test_fb
	./build/test_wake
	./build/test_wx $(FIXTURE) test/fixtures/open-meteo-greenwich-2026-09-14.json
	./build/test_gfx
	./build/test_sched_batt
	./build/test_news test/fixtures/npr-news-2026-09-13.xml test/fixtures/bbc-world-2026-09-14.xml
	./build/test_status
	./build/test_orbit test/fixtures/celestrak-iss-2026-09-13.tle
	./build/test_crew

# Render one screen on your computer to build/preview.png with live weather and
# headlines for the place and feed in config.h (config.example.h if you have no
# config.h yet). MODE: 1 weather, 2 headlines, 3 status, 4 orbital, 5 crew.
# FORECAST=file and NEWS=file use saved responses instead; PREVIEW_NOW=unix
# seconds renders at another time.
MODE ?= 1
preview:
	@mkdir -p build
	$(HOST_CC) $(PREVIEW_SRC) -lm -o build/preview
	@if [ -z "$(FORECAST)" ]; then curl -sf "$$(./build/preview --urls | sed -n 1p)" -o build/forecast.json; fi
	@if [ -z "$(NEWS)" ]; then curl -sfL "$$(./build/preview --urls | sed -n 2p)" -o build/news.xml; fi
	./build/preview $(or $(FORECAST),build/forecast.json) $(or $(NEWS),build/news.xml) build/preview.ppm $(MODE)
	python3 tools/ppm2png.py build/preview.ppm build/preview.png 2
	@echo "wrote build/preview.png"

# The README's screenshots, from the example settings and saved responses. The
# weather screen uses a stormy afternoon in Havana, which shows more placards
# than a mild day at the example location.
SHOT_NOW := 1789410600
screenshots:
	@mkdir -p build docs
	$(HOST_CC) -DPREVIEW_EXAMPLE $(PREVIEW_SRC) -lm -o build/preview-example
	PREVIEW_NOW=1789405500 PREVIEW_PLACE=HAVANA ./build/preview-example test/fixtures/open-meteo-havana-2026-09-14.json \
		test/fixtures/bbc-world-2026-09-14.xml build/shot.ppm 1 >/dev/null
	python3 tools/ppm2png.py build/shot.ppm docs/mode-1.png 2
	@for m in 2 3 4 5; do \
		PREVIEW_NOW=$(SHOT_NOW) ./build/preview-example test/fixtures/open-meteo-greenwich-2026-09-14.json \
			test/fixtures/bbc-world-2026-09-14.xml build/shot.ppm $$m >/dev/null && \
		python3 tools/ppm2png.py build/shot.ppm docs/mode-$$m.png 2 || exit 1; \
	done
	@echo "wrote docs/mode-1.png to docs/mode-5.png"

# Redraw the placard and font bitmaps from tools/assets/rasterize.html (needs Google Chrome).
assets:
	python3 tools/assets/build_assets.py

# Check firmware/orders.txt and regenerate firmware/main/orders_gen.c.
orders:
	python3 tools/orders.py

clean:
	rm -rf build firmware/build firmware/sdkconfig firmware/sdkconfig.old

idf-check:
	@test -e "$(IDF_PATH)/export.sh" || { echo "No ESP-IDF at $(IDF_PATH). Install v5.5 (see README) or run make with IDF_PATH=/path/to/esp-idf."; exit 1; }

port:
	@test -n "$(PORT)" || { echo "No $(PORT_GLOB) port. Plug the PicPak in with a data cable and hold its button so it stays awake."; exit 1; }
