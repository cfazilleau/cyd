// Île-de-France Mobilités PRIM API client.
#pragma once
#include <stddef.h>
#include "model.h"

// Next RER A departures from the configured station, towards Paris.
// Returns true on success. On failure `err` holds a short French message.
bool prim_fetch_departures(Departure *out, uint8_t *count, char *err, size_t errLen);

// Current and later-today disruptions on the line.
bool prim_fetch_disruptions(Disruption *out, uint8_t *count, char *err, size_t errLen);
