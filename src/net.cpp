#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "config.h"

// Last access point, kept in RTC memory across deep sleep: connecting straight
// to a known channel + BSSID skips the scan and saves a second or two.
RTC_DATA_ATTR static int32_t apChannel = 0;
RTC_DATA_ATTR static uint8_t apBssid[6];
static bool tryingCachedAp = false;  // first attempt of this session, before any scan

void net_begin() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    // Radio sleeps between beacons; we only poll every 30 s so latency is irrelevant.
    WiFi.setSleep(WIFI_PS_MAX_MODEM);
    WiFi.setAutoReconnect(true);
    tryingCachedAp = apChannel > 0;
    if (tryingCachedAp) WiFi.begin(WIFI_SSID, WIFI_PASSWORD, apChannel, apBssid);
    else WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Also remembers the access point once connected, and falls back to a normal
// scan if the remembered one is gone (moved channel, replaced router...).
bool net_connected() {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        apChannel = WiFi.channel();
        memcpy(apBssid, WiFi.BSSID(), sizeof(apBssid));
        tryingCachedAp = false;
        return true;
    }
    if (tryingCachedAp && (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED)) {
        Serial.printf("[wifi] cached AP failed (%d), scanning\n", (int)st);
        apChannel = 0;  // forget it; net_begin() then scans normally
        WiFi.disconnect();
        net_begin();
    }
    return false;
}

bool net_connect(uint32_t timeoutMs) {
    net_begin();
    uint32_t start = millis();
    while (!net_connected() && millis() - start < timeoutMs) delay(100);
    return net_connected();
}

void net_off() {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}

void time_begin_sync() {
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
}

bool time_synced() { return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED; }

bool clock_valid() { return time(nullptr) > 1700000000; }  // after Nov 2023
