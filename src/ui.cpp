#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Palette & symbols
// ---------------------------------------------------------------------------

namespace col {
constexpr uint32_t BG      = 0x0B0E14;
constexpr uint32_t CARD    = 0x161B24;
constexpr uint32_t CHIP    = 0x252C3A;
constexpr uint32_t LINE    = 0x1F2530;
constexpr uint32_t TEXT    = 0xF2F4F8;
constexpr uint32_t SOFT    = 0xC5CCD6;
constexpr uint32_t MUTED   = 0x8A94A6;
constexpr uint32_t RER_A   = 0xEB2132;
constexpr uint32_t GREY    = 0x3A4150;
constexpr uint32_t OK      = 0x2BD47D;
constexpr uint32_t WARN    = 0xFFB020;
constexpr uint32_t BAD     = 0xFF4D4F;
constexpr uint32_t INFO    = 0x4DA3FF;
}  // namespace col

#define SYM_CHECK   "\xEF\x81\x98"  // F058
#define SYM_WARN    "\xEF\x81\xB1"  // F071
#define SYM_INFO    "\xEF\x81\x9A"  // F05A
#define SYM_CROSS   "\xEF\x81\x97"  // F057
#define SYM_WIFI    "\xEF\x87\xAB"  // F1EB
#define SYM_WALK    "\xEF\x95\x94"  // F554

// Layout: everything that differs between landscape (320x240) and portrait (240x320).
static constexpr bool kPortrait = SCREEN_PORTRAIT;
static constexpr int kW = kPortrait ? 240 : 320, kH = kPortrait ? 320 : 240;
static constexpr int kRows = kPortrait ? 4 : 3;
static constexpr int kCardY = kPortrait ? 68 : 48, kCardH = kPortrait ? 112 : 80;
static constexpr int kMinW = kPortrait ? 80 : 84;    // red countdown block
static constexpr int kTextX = kMinW + (kPortrait ? 10 : 12);  // card text column
static constexpr int kTextW = kW - 16 - kTextX - 4;
static constexpr int kChipY = kPortrait ? 48 : 34, kStatusY = kPortrait ? 72 : 56;
static constexpr int kRowsY = kCardY + kCardH + (kPortrait ? 6 : 6), kRowH = kPortrait ? 22 : 24;
static constexpr int kFooterH = kPortrait ? 42 : 30;
static constexpr uint32_t kDetailsTimeoutMs = DETAILS_TIMEOUT_S * 1000UL;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius = 0) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text = "") {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

// Only touch LVGL when something really changes: every change means redrawing
// (and pushing over SPI) part of the screen.
static void set_text(lv_obj_t *l, const char *text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

static void set_text_color(lv_obj_t *o, uint32_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), lv_color_hex(c)))
        lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

static void set_bg(lv_obj_t *o, uint32_t c) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), lv_color_hex(c)))
        lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}

static void set_hidden(lv_obj_t *o, bool hidden) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// Large font if the text fits, smaller one otherwise (long station names).
static void set_text_fit(lv_obj_t *l, const char *text, const lv_font_t *big, const lv_font_t *small, int width) {
    const lv_font_t *f = lv_text_get_width(text, strlen(text), big, 0) <= width ? big : small;
    if (lv_obj_get_style_text_font(l, 0) != f) lv_obj_set_style_text_font(l, f, 0);
    set_text(l, text);
}

// "Saint-Germain-en-Laye" -> "St-Germain-en-Laye" for the compact rows.
static void abbreviate(char *dst, size_t n, const char *src) {
    if (strncmp(src, "Saint-", 6) == 0) snprintf(dst, n, "St-%s", src + 6);
    else snprintf(dst, n, "%s", src);
}

static void fmt_hhmm(char *buf, size_t n, time_t t) {
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(buf, n, "%H:%M", &lt);
}

static int minutes_until(time_t when, time_t now) {
    long s = (long)(when - now);
    return s <= 0 ? 0 : (int)(s / 60);
}

static void fmt_wait(char *buf, size_t n, int minutes) {
    if (minutes < 60) snprintf(buf, n, "%d min", minutes);
    else snprintf(buf, n, "%dh%02d", minutes / 60, minutes % 60);
}

// The RER logo: outlined "RER" pill followed by the red "A" disc.
static lv_obj_t *rer_logo(lv_obj_t *parent, bool large) {
    const int h = large ? 44 : 28;
    const int w = large ? 96 : 60;
    lv_obj_t *pill = box(parent, 0, 0, w, h, col::BG, large ? 12 : 8);
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pill, 2, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(col::GREY), 0);

    lv_obj_t *rer = label(pill, large ? &font_ui_16 : &font_ui_12, col::TEXT, "RER");
    lv_obj_align(rer, LV_ALIGN_LEFT_MID, large ? 9 : 5, 0);

    const int d = h - 8;
    lv_obj_t *disc = box(pill, 0, 0, d, d, col::RER_A, LV_RADIUS_CIRCLE);
    lv_obj_align(disc, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_t *a = label(disc, large ? &font_ui_20 : &font_ui_14, 0xFFFFFF, "A");
    lv_obj_center(a);
    return pill;
}

// A rounded tag with text (mission code, status).
struct Chip {
    lv_obj_t *bg;
    lv_obj_t *text;
};

static Chip chip(lv_obj_t *parent, uint32_t bg, uint32_t fg, int radius) {
    Chip c;
    c.bg = box(parent, 0, 0, LV_SIZE_CONTENT, 18, bg, radius);
    lv_obj_set_style_pad_hor(c.bg, 6, 0);
    c.text = label(c.bg, &font_ui_12, fg);
    lv_obj_center(c.text);
    return c;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

static lv_obj_t *scrSplash, *scrBoard, *scrDetails;

static struct {
    lv_obj_t *message, *spinner;
} splash;

static struct {
    lv_obj_t *clock, *liveDot, *liveText;
    // next train card
    lv_obj_t *minBlock, *minBig, *minUnit, *dest, *info, *extra, *walk;
    Chip mission, status;
    // following trains
    struct Row { lv_obj_t *row, *mission, *dest, *time, *wait; } rows[kRows];
    // traffic footer
    lv_obj_t *footer, *footIcon, *footText, *footCount;
} board;

static struct {
    lv_obj_t *bar, *barIcon, *barTitle, *barPage, *when, *text, *title, *body;
} det;

static const BoardData *current = nullptr;
static int detailsIndex = -1;  // -1 = board visible
static uint32_t lastTapMs = 0;

// ---------------------------------------------------------------------------

static lv_obj_t *new_screen() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(col::BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

static void build_splash() {
    scrSplash = new_screen();
    lv_obj_t *logo = rer_logo(scrSplash, true);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *name = label(scrSplash, &font_ui_20, col::TEXT, STATION_NAME);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 6);

    splash.message = label(scrSplash, &font_ui_14, col::MUTED);
    lv_obj_set_width(splash.message, kW - 20);
    lv_obj_set_style_text_align(splash.message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(splash.message, LV_ALIGN_CENTER, 0, 36);

    splash.spinner = lv_spinner_create(scrSplash);
    lv_spinner_set_anim_params(splash.spinner, 1000, 60);
    lv_obj_set_size(splash.spinner, 28, 28);
    lv_obj_set_style_arc_width(splash.spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(splash.spinner, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(splash.spinner, lv_color_hex(col::CARD), LV_PART_MAIN);
    lv_obj_set_style_arc_color(splash.spinner, lv_color_hex(col::RER_A), LV_PART_INDICATOR);
    lv_obj_align(splash.spinner, LV_ALIGN_CENTER, 0, 80);
}

static void build_board() {
    scrBoard = new_screen();

    // Header -----------------------------------------------------------------
    lv_obj_t *logo = rer_logo(scrBoard, false);
    lv_obj_set_pos(logo, 8, 8);

    // Landscape: logo, station, clock on one line. Portrait: logo + clock, then station.
    lv_obj_t *station = label(scrBoard, &font_ui_16, col::TEXT, STATION_NAME);
    if (kPortrait) lv_obj_set_pos(station, 10, 44);
    else lv_obj_set_pos(station, 76, 13);

    board.clock = label(scrBoard, &font_ui_20, col::TEXT, "--:--");
    lv_obj_align(board.clock, LV_ALIGN_TOP_RIGHT, -10, kPortrait ? 8 : 4);
    board.liveText = label(scrBoard, &font_ui_12, col::MUTED, "");
    lv_obj_align(board.liveText, LV_ALIGN_TOP_RIGHT, -10, kPortrait ? 47 : 27);
    board.liveDot = box(scrBoard, 0, 0, 7, 7, col::MUTED, LV_RADIUS_CIRCLE);

    // Next train card --------------------------------------------------------
    lv_obj_t *card = box(scrBoard, 8, kCardY, kW - 16, kCardH, col::CARD, 12);

    board.minBlock = box(card, 0, 0, kMinW, kCardH, col::RER_A, 12);
    board.minBig = label(board.minBlock, &font_big_44, 0xFFFFFF, "-");
    lv_obj_align(board.minBig, LV_ALIGN_CENTER, 0, kPortrait ? -10 : -9);
    board.minUnit = label(board.minBlock, &font_ui_14, 0xFFFFFF, "min");
    lv_obj_align(board.minUnit, LV_ALIGN_BOTTOM_MID, 0, kPortrait ? -12 : -7);

    // Portrait: narrower column, so the destination may take two lines.
    board.dest = label(card, kPortrait ? &font_ui_16 : &font_ui_20, col::TEXT, "");
    lv_label_set_long_mode(board.dest, LV_LABEL_LONG_DOT);
    if (kPortrait) lv_obj_set_size(board.dest, kTextW, 2 * font_ui_16.line_height);
    else lv_obj_set_width(board.dest, kTextW);
    lv_obj_set_pos(board.dest, kTextX, kPortrait ? 6 : 7);

    board.mission = chip(card, col::CHIP, col::SOFT, 4);
    lv_obj_set_pos(board.mission.bg, kTextX, kChipY);
    board.info = label(card, &font_ui_14, col::MUTED, "");
    lv_label_set_long_mode(board.info, LV_LABEL_LONG_DOT);  // error messages can be long
    lv_obj_set_size(board.info, kTextW, font_ui_14.line_height);
    lv_obj_align_to(board.info, board.mission.bg, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    board.status = chip(card, col::OK, col::BG, 9);
    lv_obj_set_pos(board.status.bg, kTextX, kStatusY);
    // "Train long": next to the status pill, or below it in portrait.
    board.extra = label(card, &font_ui_12, col::MUTED, "");
    if (kPortrait) lv_obj_set_pos(board.extra, kTextX, 93);
    else lv_obj_align_to(board.extra, board.status.bg, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    // Walking time to the station, right-aligned (chip row / "Train long" row in portrait).
    char walk[24];
    snprintf(walk, sizeof(walk), SYM_WALK " %d min", WALK_MINUTES);
    board.walk = label(card, kPortrait ? &font_ui_12 : &font_ui_14, col::MUTED, walk);
    lv_obj_align(board.walk, LV_ALIGN_TOP_RIGHT, -8, kPortrait ? 93 : kChipY + 1);
    set_hidden(board.walk, true);

    // Following trains (portrait: no room for the mission code) ---------------
    for (int i = 0; i < kRows; i++) {
        auto &r = board.rows[i];
        r.row = box(scrBoard, 8, kRowsY + i * kRowH, kW - 16, kRowH, col::BG);
        if (i > 0) {
            lv_obj_set_style_border_side(r.row, LV_BORDER_SIDE_TOP, 0);
            lv_obj_set_style_border_width(r.row, 1, 0);
            lv_obj_set_style_border_color(r.row, lv_color_hex(col::LINE), 0);
        }
        r.mission = label(r.row, &font_ui_12, col::MUTED);
        lv_obj_align(r.mission, LV_ALIGN_LEFT_MID, 6, 0);
        if (kPortrait) lv_obj_add_flag(r.mission, LV_OBJ_FLAG_HIDDEN);
        r.dest = label(r.row, &font_ui_14, col::SOFT);
        lv_label_set_long_mode(r.dest, LV_LABEL_LONG_DOT);
        lv_obj_set_size(r.dest, kPortrait ? 116 : 148, font_ui_14.line_height);
        lv_obj_align(r.dest, LV_ALIGN_LEFT_MID, kPortrait ? 2 : 52, 0);
        r.time = label(r.row, kPortrait ? &font_ui_12 : &font_ui_14, col::MUTED);
        lv_obj_set_width(r.time, kPortrait ? 40 : 46);
        lv_obj_set_style_text_align(r.time, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(r.time, LV_ALIGN_LEFT_MID, kPortrait ? 118 : 198, 0);
        r.wait = label(r.row, &font_ui_14, col::TEXT);
        lv_obj_set_width(r.wait, 54);
        lv_obj_set_style_text_align(r.wait, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(r.wait, LV_ALIGN_RIGHT_MID, kPortrait ? -2 : -6, 0);
    }

    // Traffic footer (portrait: up to two lines) -------------------------------
    board.footer = box(scrBoard, 0, kH - kFooterH, kW, kFooterH, col::CARD);
    board.footIcon = label(board.footer, &font_ui_14, col::MUTED, SYM_INFO);
    lv_obj_align(board.footIcon, LV_ALIGN_LEFT_MID, kPortrait ? 10 : 12, 0);
    board.footText = label(board.footer, kPortrait ? &font_ui_12 : &font_ui_14, col::SOFT, "Info trafic\xE2\x80\xA6");
    lv_label_set_long_mode(board.footText, LV_LABEL_LONG_DOT);
    if (kPortrait) {
        lv_obj_set_size(board.footText, 176, font_ui_12.line_height);  // 1 or 2 lines, see render_footer
    } else {
        lv_obj_set_size(board.footText, 236, font_ui_14.line_height);
    }
    lv_obj_align(board.footText, LV_ALIGN_LEFT_MID, kPortrait ? 32 : 36, 0);
    board.footCount = label(board.footer, &font_ui_12, col::MUTED, "");
    lv_obj_align(board.footCount, LV_ALIGN_RIGHT_MID, kPortrait ? -10 : -12, 0);
}

static void build_details() {
    scrDetails = new_screen();
    det.bar = box(scrDetails, 0, 0, kW, 34, col::WARN);
    det.barIcon = label(det.bar, &font_ui_16, col::BG, SYM_WARN);
    lv_obj_align(det.barIcon, LV_ALIGN_LEFT_MID, 12, 0);
    det.barTitle = label(det.bar, &font_ui_16, col::BG, "Info trafic RER A");
    lv_obj_align(det.barTitle, LV_ALIGN_LEFT_MID, 38, 0);
    det.barPage = label(det.bar, &font_ui_14, col::BG, "");
    lv_obj_align(det.barPage, LV_ALIGN_RIGHT_MID, -12, 0);

    det.when = label(scrDetails, &font_ui_12, col::MUTED, "");
    lv_obj_set_pos(det.when, 12, 41);

    // Title + body flow in a clipped column; long texts scroll slowly (see render_details).
    lv_obj_t *col = det.text = lv_obj_create(scrDetails);
    lv_obj_remove_style_all(col);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(col, 12, 58);
    lv_obj_set_size(col, kW - 24, kH - 62);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 6, 0);

    det.title = label(col, &font_ui_16, col::TEXT);
    lv_label_set_long_mode(det.title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(det.title, kW - 24);
    det.body = label(col, &font_ui_12, col::SOFT);
    lv_label_set_long_mode(det.body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(det.body, kW - 24);
    lv_obj_set_style_text_line_space(det.body, 2, 0);
}

void ui_init() {
    build_splash();
    build_board();
    build_details();
    lv_screen_load(scrSplash);
}

void ui_splash(const char *message, bool spinner) {
    set_text(splash.message, message);
    set_hidden(splash.spinner, !spinner);
    if (lv_screen_active() != scrSplash) lv_screen_load_anim(scrSplash, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void ui_show_board() {
    detailsIndex = -1;
    lv_obj_t *active = lv_screen_active();
    if (active == scrBoard) return;
    // Back from the details: slide (they came in from the right). From the splash: fade.
    if (active == scrDetails) lv_screen_load_anim(scrBoard, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
    else lv_screen_load_anim(scrBoard, LV_SCR_LOAD_ANIM_FADE_IN, 250, 0, false);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static uint32_t severity_color(Severity s) {
    switch (s) {
        case SEV_BLOCKING: return col::BAD;
        case SEV_DISRUPTED: return col::WARN;
        default: return col::INFO;
    }
}

static const char *severity_icon(Severity s) {
    switch (s) {
        case SEV_BLOCKING: return SYM_CROSS;
        case SEV_DISRUPTED: return SYM_WARN;
        default: return SYM_INFO;
    }
}

static void render_header(const BoardData &d, time_t now) {
    char buf[16];
    fmt_hhmm(buf, sizeof(buf), now);
    set_text(board.clock, buf);

    uint32_t c;
    const char *txt;
    long age = d.departuresAt ? (long)(now - d.departuresAt) : -1;
    if (d.departuresState == FETCH_NEVER) {
        c = col::MUTED;
        txt = "Connexion\xE2\x80\xA6";
    } else if (age >= 0 && age < 3 * DEPARTURES_REFRESH_S) {
        c = col::OK;
        txt = "Temps r\xC3\xA9" "el";
    } else {
        c = d.departuresState == FETCH_ERROR ? col::BAD : col::WARN;
        txt = "Hors ligne";
    }
    if (strcmp(lv_label_get_text(board.liveText), txt) != 0) {
        lv_label_set_text(board.liveText, txt);
        lv_obj_update_layout(board.liveText);
        lv_obj_align_to(board.liveDot, board.liveText, LV_ALIGN_OUT_LEFT_MID, -5, 0);

        // The dot breathes slowly while the data is live, and stays still otherwise.
        lv_anim_delete(board.liveDot, nullptr);
        lv_obj_set_style_bg_opa(board.liveDot, LV_OPA_COVER, 0);
        if (c == col::OK) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, board.liveDot);
            lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
                lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
            });
            lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
            lv_anim_set_duration(&a, 1000);
            lv_anim_set_playback_duration(&a, 1000);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        }
    }
    set_text_color(board.liveText, c);
    set_bg(board.liveDot, c);
}

// Countdown: when the number changes (5 -> 4), the new value rises into place while fading in.
static void set_minutes(const char *text) {
    const char *old = lv_label_get_text(board.minBig);
    if (strcmp(old, text) == 0) return;
    const bool animate = strcmp(old, "-") != 0;  // not on the first value
    lv_label_set_text(board.minBig, text);
    if (!animate) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, board.minBig);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t y) {
        lv_obj_set_style_translate_y((lv_obj_t *)o, y, 0);
        lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)(LV_OPA_COVER - y * LV_OPA_COVER / 12), 0);
    });
    lv_anim_set_values(&a, 12, 0);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// Landscape: one line, smaller font if needed. Portrait: up to two lines (see build_board).
static void set_dest(const char *text) {
    if (kPortrait) set_text(board.dest, text);
    else set_text_fit(board.dest, text, &font_ui_20, &font_ui_16, kTextW);
}

static void render_next(const BoardData &d, time_t now) {
    char buf[64];
    if (d.departureCount == 0) {
        set_bg(board.minBlock, col::GREY);
        set_text(board.minBig, "-");
        set_text(board.minUnit, "");
        set_hidden(board.mission.bg, true);
        set_hidden(board.status.bg, true);
        set_hidden(board.walk, true);
        set_text(board.extra, "");
        const char *title;
        if (d.departuresState == FETCH_ERROR) {
            title = "Donn\xC3\xA9" "es indisponibles";
            set_text(board.info, d.lastError);
        } else if (d.departuresState == FETCH_NEVER) {
            title = "Chargement\xE2\x80\xA6";
            set_text(board.info, "");
        } else {
            title = "Aucun train annonc\xC3\xA9";
            set_text(board.info, "");
        }
        set_dest(title);
        lv_obj_set_pos(board.info, kTextX, kChipY + 2);
        return;
    }

    const Departure &t = d.departures[0];
    const int m = minutes_until(t.when, now);

    set_dest(t.destination);
    if (t.status == TRAIN_CANCELLED) {
        set_bg(board.minBlock, col::GREY);
        set_text(board.minBig, "-");
    } else {
        set_bg(board.minBlock, col::RER_A);
        if (m < 100) { snprintf(buf, sizeof(buf), "%d", m); set_minutes(buf); }
        else set_text(board.minBig, "-");
    }
    set_text(board.minUnit, "min");
    set_hidden(board.walk, WALK_MINUTES <= 0);

    // Mission code + departure time
    set_hidden(board.mission.bg, t.mission[0] == '\0');
    set_text(board.mission.text, t.mission);
    char hhmm[8];
    fmt_hhmm(hhmm, sizeof(hhmm), t.when);
    set_text(board.info, hhmm);
    lv_obj_update_layout(board.mission.bg);
    if (t.mission[0]) lv_obj_align_to(board.info, board.mission.bg, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    else lv_obj_set_pos(board.info, kTextX, kChipY + 2);

    // Status pill
    set_hidden(board.status.bg, false);
    uint32_t pill = col::OK;
    switch (t.status) {
        case TRAIN_CANCELLED: pill = col::BAD; set_text(board.status.text, "Supprim\xC3\xA9"); break;
        case TRAIN_AT_STOP:   pill = col::INFO; set_text(board.status.text, "\xC3\x80 quai"); break;
        case TRAIN_DELAYED:
            pill = col::WARN;
            snprintf(buf, sizeof(buf), "Retard +%d min", t.delayMin);
            set_text(board.status.text, t.delayMin > 0 ? buf : "Retard");
            break;
        default: set_text(board.status.text, "\xC3\x80 l'heure"); break;
    }
    set_bg(board.status.bg, pill);
    set_text(board.extra, t.longTrain ? "Train long" : "");
    if (!kPortrait) {
        lv_obj_update_layout(board.status.bg);
        lv_obj_align_to(board.extra, board.status.bg, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    }
}

static void render_rows(const BoardData &d, time_t now) {
    char buf[16];
    for (int i = 0; i < kRows; i++) {
        auto &r = board.rows[i];
        int idx = i + 1;
        if (idx >= d.departureCount) {
            set_hidden(r.row, true);
            continue;
        }
        set_hidden(r.row, false);
        const Departure &t = d.departures[idx];
        set_text(r.mission, t.mission);
        char dest[sizeof(t.destination)];
        abbreviate(dest, sizeof(dest), t.destination);
        set_text(r.dest, dest);
        fmt_hhmm(buf, sizeof(buf), t.when);
        set_text(r.time, buf);

        bool cancelled = t.status == TRAIN_CANCELLED;
        lv_text_decor_t decor = cancelled ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE;
        if (lv_obj_get_style_text_decor(r.dest, 0) != decor) lv_obj_set_style_text_decor(r.dest, decor, 0);
        set_text_color(r.dest, cancelled ? col::MUTED : col::SOFT);

        if (cancelled) {
            set_text(r.wait, "suppr.");
            set_text_color(r.wait, col::BAD);
        } else {
            fmt_wait(buf, sizeof(buf), minutes_until(t.when, now));
            set_text(r.wait, buf);
            set_text_color(r.wait, t.status == TRAIN_DELAYED ? col::WARN : col::TEXT);
        }
    }
}

static void render_footer(const BoardData &d) {
    uint32_t bg, fg;
    const char *icon, *text;
    char count[8] = "";

    if (d.disruptionCount > 0) {
        const Disruption &first = d.disruptions[0];
        Severity worst = SEV_INFO;
        for (int i = 0; i < d.disruptionCount; i++)
            if (d.disruptions[i].active && d.disruptions[i].severity > worst) worst = d.disruptions[i].severity;
        if (!first.active) worst = SEV_INFO;
        fg = severity_color(worst);
        icon = severity_icon(worst);
        bg = worst == SEV_BLOCKING ? 0x3A1214 : worst == SEV_DISRUPTED ? 0x33260A : 0x0F2238;
        text = first.title;
        if (d.disruptionCount > 1) snprintf(count, sizeof(count), "+%d", d.disruptionCount - 1);
    } else if (d.disruptionsState == FETCH_OK) {
        bg = 0x0F2A1D;
        fg = col::OK;
        icon = SYM_CHECK;
        text = "Trafic normal sur la ligne";
    } else {
        bg = col::CARD;
        fg = col::MUTED;
        icon = SYM_INFO;
        text = d.disruptionsState == FETCH_ERROR ? "Info trafic indisponible" : "Info trafic\xE2\x80\xA6";
    }
    set_bg(board.footer, bg);
    set_text(board.footIcon, icon);
    set_text_color(board.footIcon, fg);
    set_text(board.footCount, count);

    // The label stores its text with the "…" applied, so compare against the source text.
    static char shown[sizeof(Disruption::title)];
    if (strcmp(shown, text) != 0) {
        snprintf(shown, sizeof(shown), "%s", text);
        if (kPortrait) {  // two lines only when needed, so short texts stay vertically centred
            int lines = lv_text_get_width(text, strlen(text), &font_ui_12, 0) > 176 ? 2 : 1;
            lv_obj_set_height(board.footText, lines * font_ui_12.line_height);
        }
        lv_label_set_text(board.footText, text);
    }
}

// One page per message, or a single summary page when there is none.
static int details_pages(const BoardData &d) { return d.disruptionCount > 0 ? d.disruptionCount : 1; }

// Called on every refresh while visible; `restart` (or a text change) rewinds the scroll.
static void render_details(bool restart) {
    if (!current || detailsIndex < 0 || detailsIndex >= details_pages(*current)) return;
    uint32_t color;
    const char *icon, *when, *title, *body;
    char page[16] = "";
    if (current->disruptionCount > 0) {
        const Disruption &x = current->disruptions[detailsIndex];
        color = severity_color(x.severity);
        icon = severity_icon(x.severity);
        snprintf(page, sizeof(page), "%d/%d", detailsIndex + 1, current->disruptionCount);
        when = x.active ? "En cours" : "Plus tard aujourd'hui";
        title = x.title;
        body = x.body;
    } else if (current->disruptionsState == FETCH_OK) {
        color = col::OK;
        icon = SYM_CHECK;
        when = "Aujourd'hui";
        title = "Trafic normal sur la ligne";
        body = "Aucune perturbation ni travaux signal\xC3\xA9s sur le RER A.";
    } else {
        color = col::MUTED;
        icon = SYM_INFO;
        when = "";
        title = current->disruptionsState == FETCH_ERROR ? "Info trafic indisponible" : "Chargement\xE2\x80\xA6";
        body = current->disruptionsState == FETCH_ERROR
                   ? "Les informations trafic n'ont pas pu \xC3\xAAtre r\xC3\xA9" "cup\xC3\xA9r\xC3\xA9" "es. Nouvel essai "
                     "dans quelques instants."
                   : "";
    }
    restart = restart || strcmp(lv_label_get_text(det.title), title) != 0 ||
              strcmp(lv_label_get_text(det.body), body) != 0;
    set_bg(det.bar, color);
    set_text(det.barIcon, icon);
    set_text(det.barPage, page);
    set_text(det.when, when);
    set_text(det.title, title);
    set_text(det.body, body);

    if (!restart) return;

    // Too long for the screen: pause, scroll down slowly, pause, back up, repeat.
    lv_anim_delete(det.text, nullptr);
    lv_obj_scroll_to_y(det.text, 0, LV_ANIM_OFF);
    lv_obj_update_layout(det.text);
    const int32_t overflow = lv_obj_get_scroll_bottom(det.text);
    if (overflow <= 0) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, det.text);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t y) {
        lv_obj_scroll_to_y((lv_obj_t *)obj, y, LV_ANIM_OFF);
    });
    lv_anim_set_values(&a, 0, overflow);
    lv_anim_set_duration(&a, overflow * DETAILS_SCROLL_MS_PER_PX);
    lv_anim_set_delay(&a, DETAILS_SCROLL_PAUSE_MS);
    lv_anim_set_playback_delay(&a, DETAILS_SCROLL_PAUSE_MS);
    lv_anim_set_playback_duration(&a, overflow * DETAILS_SCROLL_MS_PER_PX / 4);
    lv_anim_set_repeat_delay(&a, DETAILS_SCROLL_PAUSE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void ui_render(const BoardData &d, time_t now) {
    current = &d;
    render_header(d, now);
    render_next(d, now);
    render_rows(d, now);
    render_footer(d);
    if (detailsIndex >= details_pages(d)) ui_show_board();
    else render_details(false);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void ui_tap() {
    lastTapMs = millis();
    if (lv_screen_active() == scrSplash || !current) return;
    if (detailsIndex < 0) {
        detailsIndex = 0;
        render_details(true);
        lv_screen_load_anim(scrDetails, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    } else if (detailsIndex + 1 < details_pages(*current)) {
        detailsIndex++;
        render_details(true);
    } else {
        ui_show_board();
    }
}

void ui_loop() {
    if (detailsIndex >= 0 && millis() - lastTapMs > kDetailsTimeoutMs) ui_show_board();
}
