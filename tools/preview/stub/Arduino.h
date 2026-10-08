// Minimal Arduino shim so src/ui.cpp can be compiled on a PC for previews.
#pragma once
#include <stdint.h>
#include <string.h>
#include <time.h>
uint32_t millis();
static inline struct tm *localtime_r(const time_t *t, struct tm *r) { localtime_s(r, t); return r; }
