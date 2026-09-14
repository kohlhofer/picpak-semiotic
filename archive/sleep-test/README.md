# Sleep and wake test firmware (2026-09-13)

The main.c and awake-mode loop that proved deep sleep, button, IMU motion, IMU double-tap and timer wakes on the PicPak, kept for reference after the environmental-panel firmware replaced them. It built against the modules still in firmware/main (imu.c still has imu_find_int_pin and imu_arm_wake). It is not built by the Makefile.
