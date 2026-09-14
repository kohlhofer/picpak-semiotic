# SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
# SPDX-License-Identifier: AGPL-3.0-or-later
idf_component_register(
    SRCS "main.c" "maint.c" "wake.c" "fb.c" "epd.c" "imu.c" "keypin.c"
         "wx.c" "gfx.c" "screen.c" "assets_gen.c" "net.c" "sched.c" "batt.c" "news.c" "status.c"
         "sgp4.c" "sky.c" "pass.c" "orbit.c" "crew.c" "orders_gen.c"
    INCLUDE_DIRS ".")

# Readouts are cut to the width of their field on purpose; snprintf truncation
# there is the intended behaviour, not a bug.
set_source_files_properties("status.c" PROPERTIES COMPILE_OPTIONS "-Wno-format-truncation")
