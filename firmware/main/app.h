// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared between the wake loop (main.c) and maintenance mode (maint.c).
#pragma once
#include <stdbool.h>

void led_set(bool on);
void led_blink(int n, int ms);

// One status line on the USB console: battery, clock, forecast, next fetch.
void app_status_line(void);

// Maintenance mode: the board stays awake so USB is up for flashing and logs,
// printing status every 5 s. Returns once the button has been held 2 s.
void maint_run(void);
