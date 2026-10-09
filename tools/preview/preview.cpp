// Host-side renderer: draws the real UI (src/ui.cpp) with sample data and dumps
// raw RGB565 frames that tools/preview/to_png.py turns into PNG screenshots.
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "config.h"
#include "ui.h"

static uint32_t fakeMs = 0;
uint32_t millis() { return fakeMs; }
static uint32_t tick_cb() { return fakeMs; }

static constexpr int W = SCREEN_PORTRAIT ? 240 : 320, H = SCREEN_PORTRAIT ? 320 : 240;
static uint16_t fb[W * H];
static uint16_t drawBuf[W * H];

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * W + a->x1], &src[(y - a->y1) * w], w * 2);
    lv_display_flush_ready(d);
}

static void run(int ms) {
    for (int i = 0; i < ms / 10; i++) { fakeMs += 10; lv_timer_handler(); }
}

static void dump(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s%s.rgb565", dir, SCREEN_PORTRAIT ? "portrait_" : "", name);
    FILE *f = fopen(path, "wb");
    const uint16_t size[2] = {W, H};  // header read by to_png.py
    fwrite(size, 2, 2, f);
    fwrite(fb, 2, W * H, f);
    fclose(f);
    printf("wrote %s\n", path);
}

static Departure dep(time_t when, int delay, TrainStatus st, bool lng, const char *m, const char *d) {
    Departure x = {};
    x.when = when; x.delayMin = delay; x.status = st; x.longTrain = lng;
    strcpy(x.mission, m); strcpy(x.destination, d);
    return x;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ui_init();
    ui_splash("Connexion au Wi-Fi\xE2\x80\xA6", true);
    run(600);
    dump(out, "1_splash");

    time_t now = time(nullptr);
    now -= now % 60;
    static BoardData d;
    d.departuresState = FETCH_OK;
    d.departuresAt = now;
    d.departures[0] = dep(now + 4 * 60 + 20, 0, TRAIN_ON_TIME, true, "NELY", "Saint-Germain-en-Laye");
    d.departures[1] = dep(now + 9 * 60, 3, TRAIN_DELAYED, false, "QIKI", "Cergy-le-Haut");
    d.departures[2] = dep(now + 15 * 60, 0, TRAIN_ON_TIME, false, "ZEUS", "Poissy");
    d.departures[3] = dep(now + 21 * 60, 0, TRAIN_CANCELLED, false, "NELY", "Saint-Germain-en-Laye");
    d.departureCount = 4;
    d.disruptionsState = FETCH_OK;
    d.disruptionCount = 0;
    ui_render(d, now);
    ui_show_board();
    run(600);
    dump(out, "2_board_normal");

    ui_tap();  // no disruption: summary page
    run(600);
    dump(out, "2b_details_normal");
    ui_show_board();
    run(400);

    ui_render(d, now + 60);  // countdown 4 -> 3, caught mid-animation
    run(120);
    dump(out, "2c_countdown_anim");
    ui_render(d, now);
    run(600);

    d.disruptions[0] = {SEV_DISRUPTED, true,
        "Trafic perturb\xC3\xA9 entre Vincennes et Ch\xC3\xA2telet \xE2\x80\x93 incident technique",
        "En raison d'un incident technique \xC3\xA0 Nation, le trafic est perturb\xC3\xA9 sur l'ensemble de la "
        "ligne. Reprise estim\xC3\xA9" "e \xC3\xA0 08h30.\nNous vous invitons \xC3\xA0 emprunter la ligne 1 du m\xC3\xA9tro "
        "entre Ch\xC3\xA2teau de Vincennes et Ch\xC3\xA2telet. Les trains en provenance de Marne-la-Vall\xC3\xA9" "e "
        "et Boissy-Saint-L\xC3\xA9ger sont limit\xC3\xA9s \xC3\xA0 Vincennes. Les correspondances avec les lignes "
        "de bus 111 et 201 sont renforc\xC3\xA9" "es. Temps de parcours allong\xC3\xA9."};
    d.disruptions[1] = {SEV_INFO, false,
        "Travaux : pas de trains entre Joinville-le-Pont et Boissy-Saint-L\xC3\xA9ger \xC3\xA0 partir de 22h",
        "Des bus de remplacement circulent."};
    d.disruptionCount = 2;
    ui_render(d, now);
    run(400);
    dump(out, "3_board_disrupted");

    ui_tap();
    run(600);
    dump(out, "4_details");
    for (int i = 0; i < 12; i++) {  // the firmware re-renders every second
        ui_render(d, now);
        run(1000);
    }  // long text has scrolled to its end
    dump(out, "4b_details_scrolled");

    static BoardData empty;
    empty.departuresState = FETCH_ERROR;
    strcpy(empty.lastError, "Cl\xC3\xA9 API refus\xC3\xA9" "e (401)");
    empty.disruptionsState = FETCH_ERROR;
    ui_show_board();
    ui_render(empty, now);
    run(600);
    dump(out, "5_board_error");

    ui_splash("Bonne journ\xC3\xA9" "e !\nRetour demain \xC3\xA0 07:00", false);
    run(600);
    dump(out, "6_goodbye");
    return 0;
}
