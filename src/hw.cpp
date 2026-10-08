#include "hw.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <lvgl.h>

#include "config.h"

// Pins that are not part of the TFT_eSPI setup (see platformio.ini for the panel)
static constexpr int kTouchClk = 25, kTouchMosi = 32, kTouchMiso = 39, kTouchCs = 33, kTouchIrq = 36;
static constexpr int kBacklightChannel = 0;

static TFT_eSPI tft;
static uint16_t xpt_cmd(uint8_t cmd);
static uint8_t backlight = 0;

// LVGL renders into this buffer in strips, then we push them over SPI.
static constexpr int kScreenW = 320, kScreenH = 240;
static constexpr int kBufLines = 24;
static uint16_t *lvBuf = nullptr;
static constexpr size_t kBufBytes = kScreenW * kBufLines * sizeof(uint16_t);

// ---------------------------------------------------------------------------

// Pins latched during deep sleep: backlight + LEDs off, both SPI chip-selects idle.
struct HeldPin { int pin; int level; };
static constexpr HeldPin kHeldPins[] = {
    {BACKLIGHT_PIN, LOW}, {4, HIGH}, {16, HIGH}, {17, HIGH}, {TFT_CS, HIGH}, {kTouchCs, HIGH},
};

void hw_early_init() {
    gpio_deep_sleep_hold_dis();
    for (const HeldPin &h : kHeldPins) {
        gpio_hold_dis((gpio_num_t)h.pin);
        pinMode(h.pin, OUTPUT);
        digitalWrite(h.pin, h.level);
    }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb() { return millis(); }

void display_init() {
    tft.begin();
    tft.setRotation(SCREEN_ROTATION);
    tft.fillScreen(TFT_BLACK);

    ledcSetup(kBacklightChannel, 5000, 8);
    ledcAttachPin(BACKLIGHT_PIN, kBacklightChannel);
    ledcWrite(kBacklightChannel, 0);
    backlight = 0;

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(kScreenW, kScreenH);
    lv_display_set_flush_cb(disp, flush_cb);
    lvBuf = (uint16_t *)heap_caps_malloc(kBufBytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_display_set_buffers(disp, lvBuf, nullptr, kBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void backlight_fade(uint8_t to, uint16_t ms) {
    const int steps = 20;
    int from = backlight;
    for (int i = 1; i <= steps; i++) {
        int v = from + (to - from) * i / steps;
        ledcWrite(kBacklightChannel, v);
        lv_timer_handler();
        delay(ms / steps);
    }
    backlight = to;
}

void display_power_down() {
    if (backlight) backlight_fade(0, 300);
    ledcDetachPin(BACKLIGHT_PIN);
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, LOW);

    tft.writecommand(0x28);  // DISPOFF
    delay(20);
    tft.writecommand(0x10);  // SLPIN: the panel then draws microamps
    delay(120);
}

void hw_prepare_deep_sleep() {
    // Leave the XPT2046 powered down with PENIRQ enabled so a touch can wake us.
    digitalWrite(kTouchCs, LOW);
    xpt_cmd(0xD0);
    digitalWrite(kTouchCs, HIGH);

    // GPIO21 is not an RTC pin: without a hold it would float and the backlight
    // could glow. gpio_deep_sleep_hold_en() keeps digital pads latched in sleep.
    for (const HeldPin &h : kHeldPins) {
        pinMode(h.pin, OUTPUT);
        digitalWrite(h.pin, h.level);
        gpio_hold_en((gpio_num_t)h.pin);
    }
    gpio_deep_sleep_hold_en();
}

// ---------------------------------------------------------------------------
// XPT2046 on its own pins, bit-banged (only a few reads per second are needed)
// ---------------------------------------------------------------------------

static uint16_t xpt_cmd(uint8_t cmd) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(kTouchMosi, (cmd >> i) & 1);
        digitalWrite(kTouchClk, HIGH);
        delayMicroseconds(1);
        digitalWrite(kTouchClk, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(kTouchMosi, LOW);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
        digitalWrite(kTouchClk, HIGH);
        delayMicroseconds(1);
        v = (v << 1) | digitalRead(kTouchMiso);
        digitalWrite(kTouchClk, LOW);
        delayMicroseconds(1);
    }
    return (v >> 3) & 0x0FFF;
}

static int touch_pressure() {
    digitalWrite(kTouchCs, LOW);
    int z1 = xpt_cmd(0xB1);
    int z2 = xpt_cmd(0xC1);
    xpt_cmd(0xD0);  // last command: power-down, PENIRQ enabled
    digitalWrite(kTouchCs, HIGH);
    return z1 + 4095 - z2;
}

void touch_init() {
    pinMode(kTouchClk, OUTPUT);
    pinMode(kTouchMosi, OUTPUT);
    pinMode(kTouchCs, OUTPUT);
    pinMode(kTouchMiso, INPUT);
    pinMode(kTouchIrq, INPUT);
    digitalWrite(kTouchCs, HIGH);
    digitalWrite(kTouchClk, LOW);
    digitalWrite(kTouchCs, LOW);
    xpt_cmd(0xD0);  // power-down mode with PENIRQ enabled
    digitalWrite(kTouchCs, HIGH);
}

bool touch_irq_active() { return digitalRead(kTouchIrq) == LOW; }

bool touch_pressed() {
    if (!touch_irq_active()) return false;  // cheap check first
    return touch_pressure() > 400;
}
