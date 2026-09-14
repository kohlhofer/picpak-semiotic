// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Battery percentage from the cell voltage. Pure C.
#pragma once

// Rough single-cell Li-Po curve under light load. -1 for an invalid reading.
// On USB the cell charges and reads near full whatever its real state.
int batt_pct(int mv);
