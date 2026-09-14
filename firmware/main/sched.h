// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Time helpers for the hourly forecast schedule. Pure C.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Parse an HTTP Date header ("Sun, 13 Sep 2026 18:35:27 GMT") to unix seconds.
bool sched_http_date(const char *s, int64_t *out);

// Parse an RSS pubDate ("Sun, 13 Sep 2026 13:45:33 -0400", or GMT/UT/Z) to unix seconds.
bool sched_rfc822_date(const char *s, int64_t *out);

// When to fetch next after a fetch at `now`: 90 s past the next hour, so the
// new hour's row is already first in the forecast.
int64_t sched_next_fetch(int64_t now);

// True once the clock has been set from the network at least once.
bool sched_time_valid(int64_t now);
