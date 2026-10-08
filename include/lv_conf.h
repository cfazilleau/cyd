// LVGL 9.2 configuration. Anything not set here uses LVGL's defaults
// (see .pio/libdeps/cyd_st7789/lvgl/src/lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Memory
// Use the system heap (no big static pool in DRAM).
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

// Rendering: the UI is mostly static, a 30 fps cap is plenty.
#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE
#define LV_DRAW_SW_COMPLEX 1

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

// Fonts: our own Montserrat builds with French accents (src/fonts).
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE \
    LV_FONT_DECLARE(font_ui_12) LV_FONT_DECLARE(font_ui_14) LV_FONT_DECLARE(font_ui_16) \
    LV_FONT_DECLARE(font_ui_20) LV_FONT_DECLARE(font_big_44)
#define LV_FONT_DEFAULT &font_ui_14
#define LV_TXT_ENC LV_TXT_ENC_UTF8

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Unused features
#define LV_USE_TFT_ESPI 0
#define LV_USE_SYSMON 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_KEYBOARD 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_MENU 0
#define LV_USE_SPAN 0
#define LV_USE_MSGBOX 0
#define LV_USE_LIST 0
#define LV_USE_IMGFONT 0

#endif  // LV_CONF_H
