// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Host test for wake-cause priority. Run with `make test`.
#include "board.h"
#include "wake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define BTN (1ULL << PIN_BUTTON)
#define IMU (1ULL << PIN_IMU_INT)

int main(void) {
    CHECK(wake_classify(WAKE_SRC_NONE, 0, 0, false) == WAKE_RESET);
    CHECK(wake_classify(WAKE_SRC_NONE, 0, 0, true) == WAKE_RESET);
    CHECK(wake_classify(WAKE_SRC_TIMER, 0, 0, false) == WAKE_TIMER);
    CHECK(wake_classify(WAKE_SRC_TIMER, BTN, IMU_TAP_DOUBLE, true) == WAKE_TIMER);   // pins only count on a GPIO wake
    CHECK(wake_classify(WAKE_SRC_GPIO, BTN, 0, false) == WAKE_BUTTON);
    CHECK(wake_classify(WAKE_SRC_GPIO, BTN | IMU, IMU_TAP_DOUBLE, false) == WAKE_BUTTON);
    CHECK(wake_classify(WAKE_SRC_GPIO, IMU, 0, true) == WAKE_BUTTON);   // the press moved the frame first
    CHECK(wake_classify(WAKE_SRC_GPIO, IMU, IMU_TAP_DOUBLE, true) == WAKE_BUTTON);
    CHECK(wake_classify(WAKE_SRC_GPIO, IMU, IMU_TAP_DOUBLE, false) == WAKE_DOUBLE_TAP);
    CHECK(wake_classify(WAKE_SRC_GPIO, IMU, IMU_TAP_SINGLE, false) == WAKE_MOTION);
    CHECK(wake_classify(WAKE_SRC_GPIO, IMU, 0, false) == WAKE_MOTION);
    CHECK(wake_classify(WAKE_SRC_GPIO, 0, IMU_TAP_DOUBLE, false) == WAKE_OTHER);   // a tap bit without the IMU pin
    CHECK(wake_classify(WAKE_SRC_OTHER, BTN, 0, true) == WAKE_OTHER);
    CHECK(strcmp(wake_name(WAKE_DOUBLE_TAP), "double-tap") == 0);
    CHECK(strcmp(wake_name((wake_t)99), "?") == 0);
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("wake: all checks passed\n");
    return EXIT_SUCCESS;
}
