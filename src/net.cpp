#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "config.h"

void net_begin() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    // Radio sleeps between beacons; we only poll every 30 s so latency is irrelevant.
    WiFi.setSleep(WIFI_PS_MAX_MODEM);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }

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
