// Board hardware: ST7789 panel + LVGL glue, backlight, XPT2046 touch, LEDs.
#pragma once
#include <stdint.h>

// Call first thing after boot: releases pins held during deep sleep, LEDs off.
void hw_early_init();

void display_init();             // TFT + LVGL, backlight off
void backlight_fade(uint8_t to, uint16_t ms);
void display_power_down();       // backlight off, panel in sleep mode

void touch_init();
bool touch_pressed();            // finger on screen (IRQ line + pressure check)
bool touch_irq_active();         // raw PENIRQ level, no SPI traffic

// Latch control pins, arm the touch controller, then call esp_deep_sleep_start().
void hw_prepare_deep_sleep();
