// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// PicPak firmware settings.
//
// Copy this file to config.h in the same folder, then edit config.h. Git
// ignores config.h, so your Wi-Fi password and your crew's names stay on your
// machine. The README's "Configure" section explains every setting.
#pragma once
#include "crew.h"

// ---- Wi-Fi (required) --------------------------------------------------------
// The PicPak's radio is 2.4 GHz only. WPA2 and WPA3 personal networks work;
// captive portals (hotel, airport) and enterprise (802.1X) networks do not.
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

// ---- Location (required) -----------------------------------------------------
// Weather, sunrise and sunset, and ISS passes are all worked out for this spot.
// Decimal degrees, north and east positive. Two decimal places (about 1 km) is
// plenty. The time zone comes from the weather service, so it follows the
// coordinates and daylight saving on its own.
#define PLACE_NAME    "GREENWICH"   // shown in headers; short, in capitals
#define PLACE_LAT     51.4779
#define PLACE_LON     -0.0015
#define PLACE_ELEV_M  46            // metres above sea level; 0 is fine

// ---- Headlines (optional) ----------------------------------------------------
// Any RSS 2.0 feed over HTTPS whose items carry <title> and <pubDate>, up to
// 48 KB. Atom feeds are not supported. Items show in feed order.
#define NEWS_URL      "https://feeds.bbci.co.uk/news/world/rss.xml"
#define NEWS_LABEL    "BBC WORLD"   // shown in the header

// ---- Crew Manifest (optional) ------------------------------------------------
// Up to six crew. For each: name (up to 10 characters), rank, figure, whether
// they are human, when they sleep, and when and how they are away.
//
//   figure      PI_MAN, PI_WOMAN, PI_GIRL1 (child, pigtails), PI_GIRL2 (child,
//               ponytail) or PI_DOG
//   human       false for pets: no duties, and they keep watch when nobody is up
//   sleep       HM(hour, minute) from and to, local time; may wrap past midnight
//   away        the word shown while away ("ON DUTY", "TRAINING"), or NULL
//   away days   CREW_WEEKDAYS (Monday to Friday), or a bit mask with bit 0 as
//               Sunday: (1 << 1) | (1 << 3) is Monday and Wednesday
//
// duty turns on the daily rotation of posts (galley, hydro bay, cargo bay,
// sanitation) among the humans; sleep shows HYPERSLEEP overnight.
#define CREW_CONFIG {                                                                                       \
    .ship = "USCSS PICPAK", .company = "WEYLAND-YUTANI", .duty = true, .sleep = true, .count = 5,          \
    .member = {                                                                                             \
        /* name     rank        figure     human  sleep from   sleep to    away        away from  away to    away days */ \
        { "RIPLEY", "WARRANT",  PI_WOMAN,  true,  HM(23, 0),   HM(6, 30),  "ON DUTY",  HM(9, 0),  HM(17, 0), CREW_WEEKDAYS }, \
        { "DALLAS", "CAPTAIN",  PI_MAN,    true,  HM(23, 30),  HM(7, 0),   "ON DUTY",  HM(9, 0),  HM(17, 0), CREW_WEEKDAYS }, \
        { "NEWT",   "CADET",    PI_GIRL1,  true,  HM(20, 30),  HM(7, 0),   "TRAINING", HM(8, 0),  HM(15, 30), CREW_WEEKDAYS }, \
        { "AMY",    "CADET",    PI_GIRL2,  true,  HM(20, 0),   HM(7, 0),   "TRAINING", HM(8, 0),  HM(15, 30), CREW_WEEKDAYS }, \
        { "JONESY", "SECURITY", PI_DOG,    false, 0,           0,          NULL,       0,         0,          0 },           \
    },                                                                                                      \
}
