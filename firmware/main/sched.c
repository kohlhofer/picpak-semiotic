// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sched.h"
#include <stdio.h>
#include <string.h>

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool sched_rfc822_date(const char *s, int64_t *out) {
    static const char *MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { 0 }, zone[8] = { 0 };
    int d, y, hh, mm, ss;
    if (!s || sscanf(s, "%*3s, %d %3s %d %d:%d:%d %7s", &d, mon, &y, &hh, &mm, &ss, zone) != 7) return false;
    const char *p = strstr(MON, mon);
    if (strlen(mon) != 3 || !p || (p - MON) % 3) return false;
    if (d < 1 || d > 31 || y < 1970 || hh > 23 || mm > 59 || ss > 60 || hh < 0 || mm < 0 || ss < 0) return false;
    int offset = 0;
    if ((zone[0] == '+' || zone[0] == '-') && strlen(zone) == 5) {
        int v = 0;
        for (int i = 1; i < 5; i++) {
            if (zone[i] < '0' || zone[i] > '9') return false;
            v = v * 10 + (zone[i] - '0');
        }
        offset = (v / 100 * 3600 + v % 100 * 60) * (zone[0] == '-' ? -1 : 1);
    } else if (strcmp(zone, "GMT") && strcmp(zone, "UT") && strcmp(zone, "UTC") && strcmp(zone, "Z")) {
        return false;
    }
    unsigned m = (unsigned)((p - MON) / 3 + 1);
    *out = days_from_civil(y, m, (unsigned)d) * 86400 + hh * 3600 + mm * 60 + ss - offset;
    return true;
}

bool sched_http_date(const char *s, int64_t *out) {
    return s && strstr(s, " GMT") && sched_rfc822_date(s, out);
}

int64_t sched_next_fetch(int64_t now) {
    return (now / 3600 + 1) * 3600 + 90;
}

bool sched_time_valid(int64_t now) {
    return now > 1700000000;   // November 2023; the RTC starts near zero after power loss
}
