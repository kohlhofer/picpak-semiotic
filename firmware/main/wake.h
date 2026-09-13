// Wake-cause classification. Pure C so the priority rules are host-tested.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { WAKE_RESET, WAKE_BUTTON, WAKE_DOUBLE_TAP, WAKE_MOTION, WAKE_TIMER, WAKE_OTHER } wake_t;

// What the sleep system reports, reduced to what the classifier needs.
typedef enum { WAKE_SRC_NONE, WAKE_SRC_TIMER, WAKE_SRC_GPIO, WAKE_SRC_OTHER } wake_src_t;

// LSM6DS3TR-C TAP_SRC and WAKE_UP_SRC bits.
#define IMU_TAP_DOUBLE 0x10
#define IMU_TAP_SINGLE 0x20
#define IMU_WU_IA      0x08

// A held button wins over the IMU because pressing it also moves the frame.
// The IMU often wins the race to wake the chip, so button_down (the button
// reads pressed right after boot) counts as a button wake too.
wake_t      wake_classify(wake_src_t src, uint64_t gpio_pins, uint8_t tap_src, bool button_down);
const char *wake_name(wake_t w);
