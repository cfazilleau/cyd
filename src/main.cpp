// RER A departure board — Joinville-le-Pont towards Paris / Auber.
//
// Power strategy
//  * Inside the schedule (default 07:00-10:00): screen on, CPU at 80 MHz,
//    Wi-Fi in max modem-sleep, departures every 30 s, traffic info every 3 min.
//    The UI only redraws what changed.
//  * Outside: deep sleep. Display panel in sleep mode, backlight off, Wi-Fi off.
//    A timer wakes the board (at most every 2 h, silently, to resync the clock)
//    and it switches on at the start of the next window.
//  * Touching the screen while asleep shows the board for PEEK_SECONDS.

#include <Arduino.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "config.h"
#include "hw.h"
#include "model.h"
#include "net.h"
#include "prim_api.h"
#include "ui.h"

enum Mode { MODE_ACTIVE, MODE_PEEK };

static Mode mode = MODE_ACTIVE;
static uint32_t peekUntilMs = 0;
static bool displayOn = false;

// Data written by the network task, copied by the UI loop under the mutex.
static BoardData shared;
static BoardData view;
static SemaphoreHandle_t dataMutex;
static volatile uint32_t dataVersion = 0;
static TaskHandle_t netTaskHandle = nullptr;
static volatile bool netStopRequested = false;
static volatile bool netStopped = false;

// ---------------------------------------------------------------------------
// Schedule
// ---------------------------------------------------------------------------

static bool day_enabled(int wday) { return (ACTIVE_DAYS >> wday) & 1; }

static bool in_window(time_t now) {
    struct tm lt;
    localtime_r(&now, &lt);
    int m = lt.tm_hour * 60 + lt.tm_min;
    return day_enabled(lt.tm_wday) && m >= ACTIVE_START_MIN && m < ACTIVE_END_MIN;
}

static time_t next_window_start(time_t now) {
    struct tm lt;
    localtime_r(&now, &lt);
    for (int d = 0; d < 8; d++) {
        struct tm c = lt;
        c.tm_mday += d;
        c.tm_hour = ACTIVE_START_MIN / 60;
        c.tm_min = ACTIVE_START_MIN % 60;
        c.tm_sec = 0;
        c.tm_isdst = -1;
        time_t t = mktime(&c);
        struct tm check;
        localtime_r(&t, &check);
        if (t > now && day_enabled(check.tm_wday)) return t;
    }
    return now + 24 * 3600;
}

// ---------------------------------------------------------------------------
// Network task (core 0): keeps `shared` up to date while the UI stays fluid
// ---------------------------------------------------------------------------

static void net_task(void *) {
    static Departure deps[MAX_DEPARTURES];
    static Disruption dis[MAX_DISRUPTIONS];
    char err[64];
    uint32_t nextDepartures = millis(), nextDisruptions = millis();

    while (!netStopRequested) {
        if (!net_connected()) {
            if (!net_connect(WIFI_CONNECT_TIMEOUT_MS)) {
                xSemaphoreTake(dataMutex, portMAX_DELAY);
                shared.departuresState = FETCH_ERROR;
                snprintf(shared.lastError, sizeof(shared.lastError), "Wi-Fi indisponible");
                dataVersion++;
                xSemaphoreGive(dataMutex);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        if ((int32_t)(millis() - nextDepartures) >= 0) {
            uint8_t n = 0;
            bool ok = prim_fetch_departures(deps, &n, err, sizeof(err));
            xSemaphoreTake(dataMutex, portMAX_DELAY);
            if (ok) {
                memcpy(shared.departures, deps, sizeof(Departure) * n);
                shared.departureCount = n;
                shared.departuresState = FETCH_OK;
                shared.departuresAt = time(nullptr);
                shared.lastError[0] = '\0';
            } else {
                // Keep the previous list: the countdown stays useful for a while.
                shared.departuresState = FETCH_ERROR;
                strlcpy(shared.lastError, err, sizeof(shared.lastError));
            }
            dataVersion++;
            xSemaphoreGive(dataMutex);
            Serial.printf("[departures] %s, %u trains%s%s\n", ok ? "ok" : "failed", n, ok ? "" : ": ",
                          ok ? "" : err);
            nextDepartures = millis() + (ok ? DEPARTURES_REFRESH_S : 10) * 1000UL;
        }

        if (netStopRequested) break;

        if ((int32_t)(millis() - nextDisruptions) >= 0) {
            uint8_t n = 0;
            bool ok = prim_fetch_disruptions(dis, &n, err, sizeof(err));
            xSemaphoreTake(dataMutex, portMAX_DELAY);
            if (ok) {
                memcpy(shared.disruptions, dis, sizeof(Disruption) * n);
                shared.disruptionCount = n;
                shared.disruptionsState = FETCH_OK;
            } else if (shared.disruptionsState != FETCH_OK) {
                shared.disruptionsState = FETCH_ERROR;
            }
            dataVersion++;
            xSemaphoreGive(dataMutex);
            Serial.printf("[disruptions] %s, %u messages%s%s\n", ok ? "ok" : "failed", n,
                          ok ? "" : ": ", ok ? "" : err);
            nextDisruptions = millis() + (ok ? DISRUPTIONS_REFRESH_S : 30) * 1000UL;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
    netStopped = true;
    vTaskDelete(nullptr);
}

static void start_net_task() {
    dataMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(net_task, "net", 16384, nullptr, 1, &netTaskHandle, 0);
}

static void stop_net_task() {
    if (!netTaskHandle) return;
    netStopRequested = true;
    uint32_t start = millis();
    while (!netStopped && millis() - start < 20000) delay(50);
    netTaskHandle = nullptr;
}

// ---------------------------------------------------------------------------
// Sleep
// ---------------------------------------------------------------------------

// Runs LVGL while waiting, so spinners/animations keep moving.
template <typename Cond>
static bool wait_until(Cond cond, uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!cond() && millis() - start < timeoutMs) {
        if (displayOn) lv_timer_handler();
        delay(10);
    }
    return cond();
}

[[noreturn]] static void go_to_sleep() {
    stop_net_task();
    net_off();

    const time_t now = time(nullptr);
    uint64_t sleepS;
    time_t target = next_window_start(now);
    if (!clock_valid()) {
        sleepS = 15 * 60;  // no idea what time it is: retry later
    } else {
        long delta = (long)(target - now);
        // The RTC oscillator drifts a little: never sleep more than 2 h in one go
        // (each wake resyncs over NTP) and aim slightly early.
        sleepS = delta > 2 * 3600 ? 2 * 3600 : max(30L, delta - delta / 50);
    }

    if (displayOn) {
        if (mode == MODE_ACTIVE) {
            static const char *kDays[] = {"dimanche", "lundi", "mardi", "mercredi",
                                          "jeudi", "vendredi", "samedi"};
            char msg[64], hhmm[8];
            struct tm t, today;
            localtime_r(&target, &t);
            localtime_r(&now, &today);
            strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
            bool tomorrow = (today.tm_wday + 1) % 7 == t.tm_wday && target - now < 2 * 86400;
            snprintf(msg, sizeof(msg), "Bonne journ\xC3\xA9" "e !\nRetour %s \xC3\xA0 %s",
                     tomorrow ? "demain" : kDays[t.tm_wday], hhmm);
            ui_splash(msg, false);
            wait_until([] { return false; }, 2500);
        }
        display_power_down();
    }

    // A finger still on the screen would wake us up right away.
    wait_until([] { return !touch_irq_active(); }, 5000);

    Serial.printf("Deep sleep for %llu s\n", sleepS);
    Serial.flush();
    hw_prepare_deep_sleep();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
    esp_sleep_enable_timer_wakeup(sleepS * 1000000ULL);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------

static void prune_departed(BoardData &d, time_t now) {
    uint8_t w = 0;
    for (uint8_t r = 0; r < d.departureCount; r++)
        if (d.departures[r].when >= now - 30) d.departures[w++] = d.departures[r];
    d.departureCount = w;
}

void setup() {
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    hw_early_init();
    Serial.begin(115200);
    setenv("TZ", TIMEZONE, 1);
    tzset();
    touch_init();

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("\nRER A board, wake cause %d\n", (int)cause);

    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        // Silent wake: resync the clock and decide whether it is time to switch on.
        if (net_connect(WIFI_CONNECT_TIMEOUT_MS)) {
            time_begin_sync();
            wait_until(time_synced, 10000);
        }
        time_t now = time(nullptr);
        if (!clock_valid()) go_to_sleep();
        if (!in_window(now)) {
            if (next_window_start(now) - now > 150) go_to_sleep();
            wait_until([] { return in_window(time(nullptr)); }, 180000);  // almost there
        }
    }

    display_init();
    displayOn = true;
    ui_init();
    ui_splash("Connexion au Wi-Fi\xE2\x80\xA6", true);
    lv_timer_handler();
    backlight_fade(BACKLIGHT_LEVEL, 400);

    net_begin();
    if (!wait_until(net_connected, WIFI_CONNECT_TIMEOUT_MS)) {
        ui_splash("Wi-Fi introuvable\n\xC2\xAB " WIFI_SSID " \xC2\xBB", false);
        wait_until([] { return false; }, 5000);
        if (!clock_valid()) go_to_sleep();
    } else if (!time_synced()) {
        ui_splash("Mise \xC3\xA0 l'heure\xE2\x80\xA6", true);
        time_begin_sync();
        wait_until(time_synced, 10000);
    }
    if (!clock_valid()) {
        ui_splash("Impossible d'obtenir l'heure", false);
        wait_until([] { return false; }, 5000);
        go_to_sleep();
    }

    const time_t now = time(nullptr);
    if (in_window(now)) {
        mode = MODE_ACTIVE;
    } else {
        mode = MODE_PEEK;
        // Right after plugging in, stay on a bit longer so you can check it works.
        uint32_t s = cause == ESP_SLEEP_WAKEUP_EXT0 ? PEEK_SECONDS : 2 * PEEK_SECONDS;
        peekUntilMs = millis() + s * 1000UL;
    }
    Serial.printf("Mode: %s\n", mode == MODE_ACTIVE ? "active" : "peek");

    start_net_task();
    ui_render(view, now);
    ui_show_board();
}

void loop() {
    static uint32_t seenVersion = UINT32_MAX;
    static uint32_t lastRenderMs = 0;
    static bool wasPressed = true;  // ignore the touch that woke us up
    static uint32_t releasedAtMs = 0;

    const uint32_t ms = millis();
    const time_t now = time(nullptr);

    // Touch: one action per press.
    bool pressed = touch_pressed();
    if (pressed && !wasPressed && ms - releasedAtMs > 150) {
        ui_tap();
        if (mode == MODE_PEEK) peekUntilMs = ms + PEEK_SECONDS * 1000UL;
    }
    if (!pressed && wasPressed) releasedAtMs = ms;
    wasPressed = pressed;

    // Data / clock refresh (once per second is enough for minute countdowns).
    if (dataVersion != seenVersion || ms - lastRenderMs >= 1000) {
        if (dataVersion != seenVersion) {
            xSemaphoreTake(dataMutex, portMAX_DELAY);
            view = shared;
            seenVersion = dataVersion;
            xSemaphoreGive(dataMutex);
        }
        prune_departed(view, now);
        ui_render(view, now);
        lastRenderMs = ms;
    }
    ui_loop();

    // End of the session?
    bool over = mode == MODE_ACTIVE ? !in_window(now) : (int32_t)(ms - peekUntilMs) >= 0;
    if (over) {
        if (in_window(now)) mode = MODE_ACTIVE;  // a peek that ran into the morning window
        else go_to_sleep();
    }

    uint32_t idle = lv_timer_handler();
    delay(constrain(idle, (uint32_t)5, (uint32_t)30));
}
