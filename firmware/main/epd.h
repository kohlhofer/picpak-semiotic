// E-paper panel driver. The panel sleeps between refreshes, so every
// epd_show wakes it with a reset, draws, and puts it back to deep sleep.
#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t epd_begin(void);   // pins and SPI device; the SPI bus must already exist
esp_err_t epd_show(const uint8_t *fb);   // full refresh, blocks about 16 s
