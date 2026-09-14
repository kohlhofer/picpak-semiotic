// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "batt.h"

int batt_pct(int mv) {
    if (mv <= 0) return -1;
    static const int MV[] = { 3400, 3600, 3700, 3750, 3800, 3900, 4000, 4100, 4180 };
    static const int PCT[] = { 0, 8, 22, 35, 48, 65, 80, 92, 100 };
    const int n = sizeof MV / sizeof MV[0];
    if (mv <= MV[0]) return 0;
    if (mv >= MV[n - 1]) return 100;
    for (int i = 1; i < n; i++)
        if (mv <= MV[i]) return PCT[i - 1] + (PCT[i] - PCT[i - 1]) * (mv - MV[i - 1]) / (MV[i] - MV[i - 1]);
    return 100;
}
