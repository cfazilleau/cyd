// LVGL user interface: splash, departure board, traffic info pages.
#pragma once
#include <time.h>
#include "model.h"

void ui_init();

// Boot / status screen with the RER A logo.
void ui_splash(const char *message, bool spinner);

// Shows the departure board (with a fade if another screen was visible).
void ui_show_board();

// Refreshes every widget from `data`. Cheap when nothing changed.
void ui_render(const BoardData &data, time_t now);

// Touch: board -> traffic info pages -> back to board.
void ui_tap();

// Housekeeping (auto-return from the traffic info pages).
void ui_loop();
