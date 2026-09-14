// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// LSM6DS3TR-C accelerometer on the shared SPI bus. Gyro stays off.
#pragma once
#include <stdint.h>
#include "esp_err.h"

#define IMU_WHO_AM_I_EXPECTED 0x6A

esp_err_t imu_begin(uint8_t *who_am_i);   // SPI device and INT input, reads the ID; writes no config
esp_err_t imu_accel_mg(int *x, int *y, int *z);

// Accelerometer on at 104 Hz for one settled sample, then off again.
esp_err_t imu_sample_mg(int *x, int *y, int *z);

// Which IMU interrupt output reaches GPIO5: 1 or 2, 0 if neither, 3 if both.
int       imu_find_int_pin(void);

// Double tap and motion interrupts, latched, routed to INT1 or INT2. Idempotent.
esp_err_t imu_arm_wake(int int_pin);

// Reading the sources clears the latched interrupt.
esp_err_t imu_read_sources(uint8_t *wake_up_src, uint8_t *tap_src);

// Accelerometer and gyro off, interrupts unrouted: about 3 µA instead of 150.
esp_err_t imu_power_down(void);
