// PicPak hardware map (ESP32-C3, hw rev v0.0.1).
// Pins from varanu5/picpak-tesserae-client's reverse engineering; IMU model and
// panel ID from strings in this unit's stock firmware (simplestick V0.3.2).
#pragma once

// Shared SPI bus: e-paper panel and IMU.
#define PIN_SPI_SCLK   6
#define PIN_SPI_MOSI   3
#define PIN_SPI_MISO   4

// E-paper panel, 4.2" 400x300 BWRY, UC81xx-class controller.
#define PIN_EPD_CS     9
#define PIN_EPD_DC     8
#define PIN_EPD_RST    10
#define PIN_EPD_BUSY   20   // low while the controller is busy

// IMU, ST LSM6DS3TR-C.
#define PIN_IMU_CS     7
#define PIN_IMU_INT    5    // deep-sleep wake capable (GPIO0-5 only on the C3)

// One button, shorted to ground when pressed. The same node carries the
// battery through a divider, so it doubles as the battery ADC input.
#define PIN_BUTTON     2
#define BATT_ADC_CHAN  ADC_CHANNEL_2   // ADC1
#define BATT_DIVIDER   1.45f           // battery mV = pin mV * divider

// Status LED, active-low. Also UART0 TX, so the console must stay on USB.
#define PIN_LED        21
