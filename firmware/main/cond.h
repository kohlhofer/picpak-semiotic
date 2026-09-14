// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Environmental conditions, in the order the placard assets are generated.
// Changing the order means regenerating assets_gen.c.
#pragma once

typedef enum {
    COND_NOMINAL, COND_HEAT, COND_CRYO, COND_PRECIP, COND_WIND, COND_ELEC,
    COND_RANGE, COND_HUMID, COND_ARID, COND_RAD, COND_VIS, COND_PRES,
    COND_COUNT
} cond_t;

typedef enum { SEV_NOTED = 0, SEV_CAUTION = 1, SEV_DANGER = 2 } sev_t;
