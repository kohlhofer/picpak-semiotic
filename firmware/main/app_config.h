// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// The user's settings from config.h, with defaults for the optional ones. A
// missing config.h stops the build rather than flashing a board that can never
// join Wi-Fi.
#pragma once

#if __has_include("config.h")
#include "config.h"
#else
#error "firmware/main/config.h is missing. Copy firmware/main/config.example.h to config.h and set your Wi-Fi and location (README, Configure)."
#endif

#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD)
#error "config.h must define WIFI_SSID and WIFI_PASSWORD."
#endif
#if !defined(PLACE_LAT) || !defined(PLACE_LON)
#error "config.h must define PLACE_LAT and PLACE_LON."
#endif

#ifndef PLACE_NAME
#define PLACE_NAME "HOME"
#endif
#ifndef PLACE_ELEV_M
#define PLACE_ELEV_M 0
#endif
#ifndef NEWS_URL
#define NEWS_URL "https://feeds.bbci.co.uk/news/world/rss.xml"
#endif
#ifndef NEWS_LABEL
#define NEWS_LABEL "BBC WORLD"
#endif
#ifndef CREW_CONFIG
#include "crew.h"
#define CREW_CONFIG { .ship = "USCSS PICPAK", .company = "WEYLAND-YUTANI", .duty = false, .sleep = false, .count = 1, \
    .member = { { "JONESY", "SECURITY", PI_DOG, false, 0, 0, NULL, 0, 0, 0 } } }
#endif
