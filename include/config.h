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
#define WALK_MINUTES      0                                 // walking time to the station (0 = hidden)

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
#define PEEK_SECONDS      300
// While asleep, wake up at least this often to resync the clock (the RTC drifts).
#define MAX_SLEEP_S       (2 * 3600)

// ---------------------------------------------------------------------------
// Refresh rates (PRIM free quota is limited, keep these reasonable)
// ---------------------------------------------------------------------------
#define DEPARTURES_REFRESH_S   30
#define DISRUPTIONS_REFRESH_S  180
#define DEPARTURES_RETRY_S     10       // after a failed request
#define DISRUPTIONS_RETRY_S    30
#define HTTP_TIMEOUT_MS        15000

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
#define BACKLIGHT_PIN       21
#define BACKLIGHT_LEVEL     170         // 0..255; lower = less power
#ifndef SCREEN_ROTATION                 // (the host preview overrides it)
#define SCREEN_ROTATION     1           // 1 or 3: landscape; 0 or 2: portrait (rotated 180° from each other)
#endif
#define SCREEN_PORTRAIT     (SCREEN_ROTATION % 2 == 0)
#define SHOW_ELEVATOR_OUTAGES 0         // also show elevator/escalator messages
#define TOUCH_PRESSURE_MIN  400         // raise if the screen reacts on its own, lower if taps are missed

// Traffic info details (opened by tapping the screen)
#define DETAILS_TIMEOUT_S        30     // back to the board after this long without a tap
#define DETAILS_SCROLL_PAUSE_MS  3000   // long messages: pause at the top and at the bottom...
#define DETAILS_SCROLL_MS_PER_PX 40     // ...and scroll down at this speed (back up 4x faster)

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
#define TIMEZONE  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define CPU_FREQ_MHZ 240                // TLS 1.3 is ~2x slower at 80 MHz
