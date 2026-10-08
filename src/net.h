// Wi-Fi and clock helpers.
#pragma once
#include <stdint.h>
#include <time.h>

void net_begin();                       // start connecting (non-blocking)
bool net_connected();
bool net_connect(uint32_t timeoutMs);   // blocking
void net_off();

void time_begin_sync();                 // start NTP (non-blocking)
bool time_synced();

// True once the RTC holds a plausible date (set by NTP, kept across deep sleep).
bool clock_valid();
