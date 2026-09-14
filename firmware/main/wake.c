// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "wake.h"
#include "board.h"

wake_t wake_classify(wake_src_t src, uint64_t gpio_pins, uint8_t tap_src, bool button_down) {
    switch (src) {
    case WAKE_SRC_NONE:  return WAKE_RESET;
    case WAKE_SRC_TIMER: return WAKE_TIMER;
    case WAKE_SRC_GPIO:
        if (button_down || (gpio_pins & (1ULL << PIN_BUTTON))) return WAKE_BUTTON;
        if (!(gpio_pins & (1ULL << PIN_IMU_INT))) return WAKE_OTHER;
        return (tap_src & IMU_TAP_DOUBLE) ? WAKE_DOUBLE_TAP : WAKE_MOTION;
    default:             return WAKE_OTHER;
    }
}

const char *wake_name(wake_t w) {
    static const char *const names[] = { "reset", "button", "double-tap", "motion", "timer", "other" };
    return (unsigned)w < sizeof names / sizeof names[0] ? names[w] : "?";
}
