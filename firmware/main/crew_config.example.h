// The crew for the Crew Manifest. Copy to crew_config.h, which git ignores, and
// put in real names. Times are local minutes after midnight via HM(hour, minute);
// a sleep window may wrap past midnight. Figures: PI_MAN, PI_WOMAN, PI_GIRL1,
// PI_GIRL2, PI_DOG. Non-humans get no duty and keep watch when nobody is up.
#pragma once
#include "crew.h"

#define CREW_CONFIG {                                                                                       \
    .ship = "USCSS PICPAK", .company = "WEYLAND-YUTANI", .duty = true, .sleep = true, .count = 5,        \
    .member = {                                                                                             \
        { "CREW 1", "COMMAND", PI_MAN, true, HM(23, 0), HM(6, 30), "ON DUTY", HM(9, 0), HM(17, 0), CREW_WEEKDAYS },   \
        { "CREW 2", "COMMAND", PI_WOMAN, true, HM(23, 0), HM(6, 30), "ON DUTY", HM(9, 0), HM(17, 0), CREW_WEEKDAYS }, \
        { "CREW 3", "CADET", PI_GIRL1, true, HM(20, 30), HM(7, 0), "TRAINING", HM(8, 0), HM(15, 30), CREW_WEEKDAYS }, \
        { "CREW 4", "CADET", PI_GIRL2, true, HM(20, 30), HM(7, 0), "TRAINING", HM(8, 0), HM(15, 30), CREW_WEEKDAYS }, \
        { "SHIPDOG", "SECURITY", PI_DOG, false, 0, 0, NULL, 0, 0, 0 },                                     \
    },                                                                                                      \
}
