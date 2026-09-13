// Battery percentage from the cell voltage. Pure C.
#pragma once

// Rough single-cell Li-Po curve under light load. -1 for an invalid reading.
// On USB the cell charges and reads near full whatever its real state.
int batt_pct(int mv);
