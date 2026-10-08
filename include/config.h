// User settings. Credentials live in secrets.h (see secrets.example.h).
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h: copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

// ---------------------------------------------------------------------------
// Station / line (IDFM referential, https://data.iledefrance-mobilites.fr)
// ---------------------------------------------------------------------------
#define STATION_NAME      "Joinville-le-Pont"
#define STOP_AREA_ID      "43135"                           // ZdA Joinville-le-Pont (railStation)
#define LINE_ID           "C01742"                          // RER A

// Trains are kept only if their destination does NOT contain one of these
// (case- and accent-insensitive). From Joinville every other RER A train runs
// west through Nation, Gare de Lyon, Châtelet and Auber.
#define EASTBOUND_DESTINATIONS \
    { "boissy", "sucy", "varenne", "saint-maur", "joinville", "marne-la-vallee", "chessy", "torcy" }

// ---------------------------------------------------------------------------
// Schedule: when the screen is on (local time, Europe/Paris)
// ---------------------------------------------------------------------------
#define ACTIVE_START_MIN  (7 * 60)      // 07:00
#define ACTIVE_END_MIN    (10 * 60)     // 10:00
// Bitmask of active days, bit 0 = Sunday ... bit 6 = Saturday.
// 0x7F = every day, 0x3E = Monday to Friday only.
#define ACTIVE_DAYS       0x7F

// Outside the schedule the board is in deep sleep. Touching the screen wakes it
// up for this long to show the current departures, then it goes back to sleep.
#define PEEK_SECONDS      60

// ---------------------------------------------------------------------------
// Refresh rates (PRIM free quota is limited, keep these reasonable)
// ---------------------------------------------------------------------------
#define DEPARTURES_REFRESH_S   30
#define DISRUPTIONS_REFRESH_S  180

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
#define BACKLIGHT_PIN       21
#define BACKLIGHT_LEVEL     170         // 0..255; lower = less power
#define SCREEN_ROTATION     1           // 1 or 3 (landscape, USB on the right/left)
#define SHOW_ELEVATOR_OUTAGES 0         // also show elevator/escalator messages

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
#define TIMEZONE  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define CPU_FREQ_MHZ 80                 // 80 MHz is plenty and saves power
