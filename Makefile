SHELL    := /bin/bash
IDF_PATH ?= $(HOME)/esp/esp-idf-v5.5.5
PORT     ?= $(firstword $(wildcard /dev/cu.usbmodem*))
DURATION ?= 30

# Homebrew's default python3 is newer than ESP-IDF 5.5 supports; use the IDF venv.
IDF_ENV := export PATH="$(HOME)/.espressif/python_env/idf5.5_py3.13_env/bin:$$PATH" \
	IDF_PATH="$(IDF_PATH)" && source "$(IDF_PATH)/export.sh" >/dev/null
IDF     := $(IDF_ENV) && idf.py -C firmware

.PHONY: build flash monitor log test assets orders preview restore clean port

build:
	$(IDF) build

flash: build port
	$(IDF) -p $(PORT) flash

# Interactive console; needs a real terminal. Ctrl-] quits.
monitor: port
	$(IDF) -p $(PORT) monitor

# Print the console for DURATION seconds without resetting the board. Follows
# it through deep sleep and timestamps every line.
log:
	$(IDF_ENV) && python tools/log.py '/dev/cu.usbmodem*' $(DURATION)

CJSON   := $(IDF_PATH)/components/json/cJSON
HOST_CC := cc -std=gnu11 -Wall -Wextra -Werror -Ifirmware/main -I$(CJSON)
SCREEN  := firmware/main/fb.c firmware/main/gfx.c firmware/main/assets_gen.c
WX      := firmware/main/wx.c $(CJSON)/cJSON.c
FIXTURE := test/fixtures/open-meteo-cary-2026-09-13.json
ORBIT   := firmware/main/sgp4.c firmware/main/sky.c firmware/main/pass.c firmware/main/orbit.c
CREW    := firmware/main/crew.c firmware/main/orders_gen.c

test:
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
	./build/test_wx $(FIXTURE)
	./build/test_gfx
	./build/test_sched_batt
	./build/test_news test/fixtures/npr-news-2026-09-13.xml
	./build/test_status
	./build/test_orbit test/fixtures/celestrak-iss-2026-09-13.tle
	./build/test_crew

# Redraw the placard and font bitmaps from tools/assets/rasterize.html.
assets:
	python3 tools/assets/build_assets.py

# Check firmware/orders.txt and regenerate firmware/main/orders_gen.c.
orders:
	python3 tools/orders.py

# Render a screen from live Cary weather and NPR headlines to build/preview.png.
# FORECAST=path and NEWS=path render saved responses instead. MODE picks the
# screen: 1 environmental panel, 2 System Updates, 3 System Status, 4 Orbital
# Tracking, 5 Crew Manifest.
MODE ?= 1
preview:
	@mkdir -p build
	$(HOST_CC) $(SCREEN) $(WX) firmware/main/news.c firmware/main/sched.c firmware/main/status.c firmware/main/batt.c $(ORBIT) $(CREW) firmware/main/screen.c tools/preview.c -lm -o build/preview
	@if [ -z "$(FORECAST)" ]; then curl -sf "$$(sed -n 's/.*WX_URL "\(.*\)" \\/\1/p;s/^ *"\(.*\)" \\$$/\1/p;s/^ *"\(.*\)"$$/\1/p' firmware/main/wx.h | tr -d '\n')" -o build/forecast.json; fi
	@if [ -z "$(NEWS)" ]; then curl -sf "$$(sed -n 's/.*NEWS_URL *"\(.*\)"/\1/p' firmware/main/news.h)" -o build/news.xml; fi
	./build/preview $(or $(FORECAST),build/forecast.json) $(or $(NEWS),build/news.xml) build/preview.ppm $(MODE)
	python3 tools/ppm2png.py build/preview.ppm build/preview.png 2
	@echo "wrote build/preview.png"

# Put this unit's stock firmware back. Takes 5-10 minutes, starting with a
# silent erase that looks stuck. ROM loader only: the stub corrupts big writes here.
restore: port
	$(IDF_ENV) && esptool.py --chip esp32c3 -p $(PORT) --no-stub write_flash --flash_size 16MB 0x0 backup/stock_16mb_1.bin

clean:
	rm -rf build firmware/build firmware/sdkconfig firmware/sdkconfig.old

port:
	@test -n "$(PORT)" || { echo "No /dev/cu.usbmodem* port. Plug the PicPak in with a data cable; hold the button if it is asleep."; exit 1; }
