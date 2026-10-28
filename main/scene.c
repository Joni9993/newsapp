#include "scene.h"
#include "gfx.h"
#include "net.h"
#include "ota.h"
#include "sources.h"
#include "text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"

#define C_WHITE RGB(255, 255, 255)
#define MAILBOX_X 96
#define STUMP_X   12

static uint32_t t, rng = 0x5EED;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rr(int a, int b) { return a + (int)(rnd() % (uint32_t)(b - a + 1)); }
static float rf(float a, float b) { return a + (b - a) * (rnd() & 0xFFFF) / 65535.0f; }

typedef struct { float x, y, vx, vy; uint16_t c; int16_t life; uint8_t kind; } sp_t;
enum { K_RAIN, K_SNOW, K_LEAF, K_BIRD, K_BUTTERFLY, K_FIREFLY, K_PETAL, K_DRONE, K_EMBER, K_SMOKE };
static int theme;   /* 0 = Landschaft, 1 = zerfallene Stadt */
#define MAXS 70
static sp_t S[MAXS];

static void add(float x, float y, float vx, float vy, uint16_t c, int life, int kind)
{
    for (int i = 0; i < MAXS; i++) if (S[i].life <= 0) { S[i] = (sp_t){ x, y, vx, vy, c, (int16_t)life, (uint8_t)kind }; return; }
}

/* ---------------------------------------------------------------- Zeit und Jahreszeit */

static struct tm now_tm(void)
{
    struct tm lt = { .tm_hour = 12, .tm_mon = 5 };
    if (net_time_valid()) { time_t n = time(NULL); localtime_r(&n, &lt); }
    return lt;
}

enum { SPRING, SUMMER, AUTUMN, WINTER };
static int season(const struct tm *lt)
{
    int m = lt->tm_mon;
    return m >= 2 && m <= 4 ? SPRING : m >= 5 && m <= 7 ? SUMMER : m >= 8 && m <= 10 ? AUTUMN : WINTER;
}

/* 0 = Nacht ... 256 = voller Tag, mit Daemmerung um Sonnenauf- und -untergang */
static int daylight(const struct tm *lt, const weather_t *w)
{
    int m = lt->tm_hour * 60 + lt->tm_min;
    int rise = w->sunrise_min ? w->sunrise_min : 7 * 60, set = w->sunset_min ? w->sunset_min : 19 * 60;
    if (m < rise - 40 || m > set + 40) return 0;
    if (m < rise + 40) return (m - (rise - 40)) * 256 / 80;
    if (m > set - 40) return (set + 40 - m) * 256 / 80;
    return 256;
}

static int mix(int a, int b, int k) { return a + (b - a) * k / 256; }

static bool is_rain(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95; }
static bool is_snow(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }

/* ---------------------------------------------------------------- Takt */

/* Alles, was Gleitkomma oder Uhrzeit braucht, wird einmal je Takt berechnet - nicht in jedem
 * der zehn Bildstreifen: der C6 hat keine Gleitkomma-Einheit, sinf() ist teuer. */
static struct tm C_lt;
static weather_t C_w;
static int C_day, C_se, C_sun_x, C_sun_y, C_sway;
static bool C_sun;
static int8_t C_lean[GW], C_flower[9];
static uint8_t H1[GW], H2[GW];
static bool hills_ok;

void scene_tick(void)
{
    t++;
    struct tm lt = now_tm();
    weather_t w;
    sources_weather(&w);
    int day = daylight(&lt, &w), se = season(&lt);
    C_lt = lt; C_w = w; C_day = day; C_se = se;
    if (!hills_ok) {
        for (int x = 0; x < GW; x++) {
            H1[x] = (uint8_t)(54 + (int)(sinf(x * 0.06f + 1.0f) * 3 + sinf(x * 0.13f) * 2));
            H2[x] = (uint8_t)(59 + (int)(sinf(x * 0.09f + 2.5f) * 3));
        }
        hills_ok = true;
    }
    {   /* Sonne bzw. Mond auf einem Bogen von links unten nach rechts unten */
        int m = lt.tm_hour * 60 + lt.tm_min;
        int rise = w.sunrise_min ? w.sunrise_min : 420, set = w.sunset_min ? w.sunset_min : 1140;
        C_sun = m >= rise && m <= set;
        float f = C_sun ? (float)(m - rise) / (set - rise) : (float)((m - set + 1440) % 1440) / (1440 - (set - rise));
        C_sun_x = (int)(8 + f * (GW - 16));
        C_sun_y = (int)(SCENE_GROUND - 12 - sinf(f * 3.14159f) * 40);
    }
    C_sway = (int)(sinf(t * 0.03f) * 1.2f);
    int amp = (w.valid ? w.wind : 8) > 25 ? 2 : 1;
    for (int x = 1; x < GW; x += 3) C_lean[x] = (int8_t)(sinf(t * 0.07f + x * 0.3f) * amp + 0.5f);
    for (int i = 0; i < 9; i++) C_flower[i] = (int8_t)(sinf(t * 0.06f + i) * 0.8f + 0.5f);

    if (theme) {   /* Stadt: saurer Regen, Asche, Drohnen, Glut und Rauch aus den Schornsteinen */
        if (w.valid && is_rain(w.code)) for (int k = 0; k < (w.code >= 63 ? 3 : 1); k++)
            add(rf(-10, GW), SCENE_TOP, -0.3f, rf(2.4f, 3.2f), RGB(150, 200, 120), 40, K_RAIN);
        if ((w.valid && is_snow(w.code) && rr(0, 1) == 0) || rr(0, 25) == 0)
            add(rf(0, GW), SCENE_TOP, rf(-0.2f, 0.2f), rf(0.2f, 0.4f), RGB(150, 145, 140), 260, K_SNOW);
        if (rr(0, 700) == 0) { bool ltr = rr(0, 1); add(ltr ? -6 : GW + 6, rf(14, 36), ltr ? 0.5f : -0.5f, 0, RGB(30, 30, 34), 300, K_DRONE); }
        if (day < 128 && rr(0, 40) == 0) add(rf(30, 80), 66, rf(-0.1f, 0.1f), -rf(0.2f, 0.5f), RGB(255, 140, 40), 90, K_EMBER);
        if (t % 6 == 0) add(34 + rf(-1, 1), 40, 0.12f, -0.18f, RGB(120, 110, 104), 160, K_SMOKE);
        if (t % 8 == 3) add(80 + rf(-1, 1), 33, 0.10f, -0.15f, RGB(110, 100, 96), 170, K_SMOKE);
        goto move;
    }
    if (w.valid && is_rain(w.code)) for (int k = 0; k < (w.code >= 63 ? 3 : 1); k++)
        add(rf(-10, GW), SCENE_TOP, -0.4f, rf(2.4f, 3.2f), RGB(160, 190, 235), 40, K_RAIN);
    if (w.valid && is_snow(w.code) && rr(0, 2) == 0)
        add(rf(0, GW), SCENE_TOP, rf(-0.2f, 0.2f), rf(0.25f, 0.5f), C_WHITE, 220, K_SNOW);
    if (se == AUTUMN && rr(0, 70) == 0) {   /* Blaetter vom Baum */
        static const uint16_t LC[3] = { RGB(220, 120, 30), RGB(200, 60, 30), RGB(230, 180, 50) };
        add(STUMP_X + rf(-6, 6), 46, rf(0.1f, 0.4f), rf(0.2f, 0.4f), LC[rr(0, 2)], 140, K_LEAF);
    }
    if (se == SPRING && rr(0, 90) == 0) add(STUMP_X + rf(-6, 6), 46, rf(0.1f, 0.4f), rf(0.15f, 0.3f), RGB(255, 190, 210), 160, K_PETAL);
    if (day > 128 && rr(0, 900) == 0) {   /* ein Vogel zieht vorbei */
        bool ltr = rr(0, 1);
        add(ltr ? -6 : GW + 6, rf(14, 34), ltr ? 0.7f : -0.7f, 0, RGB(40, 40, 50), 260, K_BIRD);
    }
    if (se == SUMMER && day > 128 && rr(0, 500) == 0) add(rf(20, 80), 72, rf(-0.3f, 0.3f), 0, RGB(250, 220, 90), 400, K_BUTTERFLY);
    if ((se == SUMMER || se == SPRING) && day < 60 && rr(0, 120) == 0) add(rf(5, GW - 5), rf(58, 74), 0, 0, RGB(220, 255, 120), 300, K_FIREFLY);

move:
    for (int i = 0; i < MAXS; i++) {
        sp_t *s = &S[i];
        if (s->life <= 0) continue;
        s->life--;
        switch (s->kind) {
        case K_LEAF: case K_PETAL: s->vx = 0.3f + sinf((t + i * 30) * 0.08f) * 0.4f; break;
        case K_BUTTERFLY: s->vx = sinf((t + i * 50) * 0.05f) * 0.5f; s->vy = sinf((t + i * 17) * 0.2f) * 0.4f; break;
        case K_FIREFLY: s->vx = sinf((t + i * 40) * 0.03f) * 0.2f; s->vy = cosf((t + i * 23) * 0.04f) * 0.15f; break;
        case K_EMBER: s->vx = sinf((t + i * 20) * 0.1f) * 0.2f; break;
        case K_SMOKE: s->vx = 0.12f + sinf((t + i * 13) * 0.05f) * 0.08f; break;
        }
        s->x += s->vx; s->y += s->vy;
        if ((s->kind == K_RAIN || s->kind == K_SNOW || s->kind == K_LEAF || s->kind == K_PETAL) && s->y >= SCENE_GROUND + rr(0, 10)) s->life = 0;
        if (s->x < -10 || s->x > GW + 10) s->life = 0;
    }
}


void scene_set_theme(int th) { theme = th; memset(S, 0, sizeof S); }

/* ================================================================ Die zerfallene Stadt (Roboter) */

static uint8_t FAR_TOP[GW], NEAR_TOP[GW];
static bool city_ok;

static uint32_t city_rng = 0xC17E;
static uint32_t crnd(void) { city_rng ^= city_rng << 13; city_rng ^= city_rng >> 17; city_rng ^= city_rng << 5; return city_rng; }

static void build_city(void)
{
    for (int pass = 0; pass < 2; pass++) {
        uint8_t *top = pass ? NEAR_TOP : FAR_TOP;
        for (int x = 0; x < GW;) {
            int w = 5 + (int)(crnd() % 8), h = pass ? 10 + (int)(crnd() % 18) : 16 + (int)(crnd() % 22);
            bool broken = crnd() % 3 == 0;
            for (int i = 0; i < w && x < GW; i++, x++) {
                int tp = SCENE_GROUND - h;
                if (broken) tp += (int)(crnd() % 5);   /* eingestuerzte Dachkante */
                top[x] = (uint8_t)tp;
            }
        }
    }
    city_ok = true;
}

static void draw_back_city(void)
{
    if (!city_ok) build_city();
    int day = C_day, by = gfx_band_y0();
    bool grey = C_w.valid && C_w.code >= 3, dusk = day > 0 && day < 256;
    /* Smog-Himmel: tags braun-orange, nachts fast schwarz mit rotem Schimmer am Horizont */
    for (int y = by; y < by + GFX_BAND && y < SCENE_GROUND; y++) {
        int k = (y - SCENE_TOP) * 256 / (SCENE_GROUND - SCENE_TOP);
        if (k < 0) k = 0;
        int r = mix(mix(14, 70, k), mix(grey ? 100 : 120, grey ? 175 : 215, k), day);
        int g = mix(mix(10, 30, k), mix(grey ? 88 : 100, grey ? 120 : 140, k), day);
        int b = mix(mix(14, 28, k), mix(grey ? 80 : 85, grey ? 95 : 80, k), day);
        if (dusk) r += (256 - abs(day - 128) * 2) * k / 256 * 60 / 256;
        gfx_fill(0, y, GW, 1, RGB(r > 255 ? 255 : r, g, b));
    }
    if (by < SCENE_GROUND) {
        /* matte Sonne im Smog, blasser Mond */
        if (C_sun) { gfx_fill(C_sun_x - 2, C_sun_y - 2, 5, 5, RGB(235, 205, 150)); gfx_fill(C_sun_x - 3, C_sun_y - 1, 7, 3, RGB(235, 205, 150)); }
        else gfx_fill(C_sun_x - 2, C_sun_y - 2, 4, 5, RGB(180, 170, 150));
        /* Smogschwaden */
        uint16_t smog = RGB(mix(40, 140, day), mix(32, 112, day), mix(30, 92, day));
        for (int i = 0; i < 4; i++) {
            int x = (int)((t / (7 + i * 3) + i * 37) % (GW + 40)) - 30, y = SCENE_TOP + 6 + i * 7;
            gfx_fill(x, y, 26, 2, smog);
            gfx_fill(x + 6, y - 1, 12, 1, smog);
        }
    }
    /* Skyline: hinten im Dunst, vorn dunkel mit Fenstern */
    uint16_t far = RGB(mix(26, 120, day), mix(20, 96, day), mix(24, 86, day));
    uint16_t near = RGB(mix(14, 62, day), mix(12, 52, day), mix(15, 50, day));
    for (int x = 0; x < GW; x++) gfx_fill(x, FAR_TOP[x], 1, SCENE_GROUND - FAR_TOP[x], far);
    /* Kran mit blinkender Warnlampe */
    uint16_t cr = RGB(mix(20, 70, day), mix(18, 56, day), mix(20, 50, day));
    gfx_fill(70, 18, 1, SCENE_GROUND - 18, cr);
    gfx_fill(60, 18, 33, 1, cr);
    gfx_fill(88, 19, 1, 8, cr);
    gfx_px(88, 27, cr);
    if ((t / 20) & 1) gfx_px(70, 17, RGB(255, 60, 50));
    for (int x = 0; x < GW; x++) {
        int tp = NEAR_TOP[x];
        gfx_fill(x, tp, 1, SCENE_GROUND - tp, near);
        if (x % 3 != 1) continue;
        /* nur die Fensterreihen, die in diesem Bildstreifen liegen */
        int y = tp + 3;
        if (y < by) y += (by - y + 3) / 4 * 4;
        for (; y < SCENE_GROUND - 2 && y < by + GFX_BAND; y += 4) {
            uint32_t h = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
            bool lit = day < 140 && h % 7 == 0 && (((t / 50) + h) % 23 != 0);
            gfx_px(x, y, lit ? RGB(255, 180, 70) : RGB(mix(8, 40, day), mix(8, 34, day), mix(10, 34, day)));
        }
    }
}

static void draw_lamp_city(int day)
{
    /* krumme Strassenlaterne links: flackert nachts */
    uint16_t pole = RGB(mix(24, 60, day), mix(24, 60, day), mix(28, 66, day));
    gfx_fill(STUMP_X, 38, 1, SCENE_GROUND - 38, pole);
    gfx_line(STUMP_X, 38, STUMP_X + 5, 37, pole);
    bool on = day < 140 && ((t / 3) % 37 != 0) && ((t / 7) % 53 != 0);
    gfx_fill(STUMP_X + 5, 38, 3, 1, on ? RGB(255, 200, 110) : RGB(60, 56, 50));
    if (on) for (int i = 0; i < 6; i++) gfx_px(STUMP_X + 4 + (i * 3) % 6, 41 + i * 3, RGB(120, 96, 60));
}

static void draw_front_city(void)
{
    int day = C_day, by = gfx_band_y0();
    if (by + GFX_BAND <= SCENE_GROUND - 30) return;
    /* Asphalt mit Rissen und verblasster Mittellinie */
    for (int y = SCENE_GROUND; y < GH; y++)
        gfx_fill(0, y, GW, 1, y < SCENE_GROUND + 2 ? RGB(mix(30, 92, day), mix(28, 86, day), mix(28, 82, day))
                                                   : RGB(mix(22, 62, day), mix(21, 58, day), mix(21, 56, day)));
    if (C_se == WINTER) gfx_fill(0, SCENE_GROUND, GW, 2, RGB(mix(60, 170, day), mix(58, 165, day), mix(56, 160, day)));   /* Asche */
    uint16_t crack = RGB(mix(10, 34, day), mix(10, 32, day), mix(10, 30, day));
    gfx_line(30, 70, 38, 72, crack); gfx_line(38, 72, 41, 77, crack); gfx_line(60, 68, 66, 71, crack); gfx_line(75, 75, 82, 74, crack);
    for (int x = 4; x < GW; x += 12) if ((x / 12) % 3 != 1) gfx_fill(x, 74, 6, 1, RGB(mix(60, 150, day), mix(52, 130, day), mix(20, 50, day)));
    /* Schutt und ein alter Reifen */
    uint16_t rub = RGB(mix(30, 110, day), mix(28, 100, day), mix(28, 94, day));
    gfx_fill(42, 64, 7, 2, rub); gfx_fill(44, 62, 3, 2, rub); gfx_px(49, 65, rub);
    gfx_fill(64, 65, 5, 1, rub); gfx_px(66, 64, rub);
    uint16_t tire = RGB(mix(10, 30, day), mix(10, 30, day), mix(10, 32, day));
    gfx_fill(52, 74, 5, 4, tire); gfx_fill(53, 75, 3, 2, RGB(mix(20, 60, day), mix(20, 56, day), mix(20, 54, day)));
    /* Fass (Sitzplatz links) */
    uint16_t rust = RGB(mix(50, 150, day), mix(26, 76, day), mix(16, 38, day));
    uint16_t ring = RGB(mix(30, 90, day), mix(16, 46, day), mix(10, 24, day));
    gfx_fill(20, 68, 9, GH - 68, rust);
    gfx_fill(20, 70, 9, 1, ring);
    gfx_fill(20, 75, 9, 1, ring);
    gfx_px(24, 72, RGB(220, 200, 40));
    /* Ladestation (Sitzplatz rechts): Bildschirm, blinkende LED, Kabel */
    uint16_t box = RGB(mix(40, 100, day), mix(42, 106, day), mix(46, 116, day));
    gfx_fill(95, 66, 2, GH - 66, RGB(mix(20, 60, day), mix(20, 60, day), mix(24, 66, day)));
    gfx_fill(91, 58, 11, 8, box);
    gfx_fill(93, 60, 4, 2, RGB(60, mix(120, 220, day), 90));
    gfx_px(99, 60, (t / 15) & 1 ? RGB(60, 255, 90) : RGB(20, 80, 30));
    gfx_line(101, 64, 104, 70, RGB(20, 20, 24));
    gfx_line(104, 70, 106, 78, RGB(20, 20, 24));
}

static void draw_particles(int front);
static void draw_lamp_city(int day);

/* ---------------------------------------------------------------- Hinten */

static void draw_cloud(int x, int y, int sz, uint16_t c)
{
    gfx_fill(x, y + 2, 10 + sz * 4, 4, c);
    gfx_fill(x + 3, y, 5 + sz, 3, c);
    gfx_fill(x + 8, y - 1 - sz / 2, 6 + sz, 4, c);
}

void scene_draw_back(void)
{
    if (theme) { draw_back_city(); draw_lamp_city(C_day); draw_particles(0); return; }
    struct tm lt = C_lt;
    weather_t w = C_w;
    int day = C_day, by = gfx_band_y0();
    bool grey = w.valid && w.code >= 3, dusk = day > 0 && day < 256;

    /* Himmel: Nacht - Daemmerung - Tag; bei Bewoelkung grauer */
    for (int y = by; y < by + GFX_BAND && y < SCENE_GROUND; y++) {
        int k = (y - SCENE_TOP) * 256 / (SCENE_GROUND - SCENE_TOP);
        if (k < 0) k = 0;
        int tr = grey ? mix(105, 175, k) : mix(60, 170, k), tg = grey ? mix(118, 182, k) : mix(130, 214, k), tb = grey ? mix(138, 194, k) : mix(215, 242, k);
        int nr = mix(8, 34, k), ng = mix(10, 28, k), nb = mix(30, 66, k);
        int r = mix(nr, tr, day), g = mix(ng, tg, day), b = mix(nb, tb, day);
        if (dusk) { int glow = (256 - abs(day - 128) * 2) * k / 256; r += glow * 90 / 256; g += glow * 30 / 256; }   /* Morgen-/Abendrot */
        gfx_fill(0, y, GW, 1, RGB(r > 255 ? 255 : r, g, b));
    }
    if (by >= SCENE_GROUND + 2) goto hills;

    /* Sterne */
    if (day < 140) for (int i = 0; i < 24; i++) {
        int x = (i * 53 + 11) % GW, y = SCENE_TOP + 2 + (i * 29) % 40;
        if (((t / 6 + i * 7) % 50) > 2) gfx_px(x, y, day < 60 ? RGB(210, 210, 240) : RGB(150, 150, 190));
    }

    /* Sonne bzw. Mond auf einem Bogen von links unten nach rechts unten */
    {
        bool sun = C_sun;
        int x = C_sun_x, y = C_sun_y;
        if (!(grey && sun)) {
            if (sun) {
                gfx_fill(x - 3, y - 2, 7, 5, RGB(255, 220, 80));
                gfx_fill(x - 2, y - 3, 5, 7, RGB(255, 220, 80));
                gfx_fill(x - 1, y - 1, 3, 3, RGB(255, 250, 200));
            } else {
                gfx_fill(x - 2, y - 3, 5, 7, RGB(240, 236, 210));
                gfx_fill(x - 3, y - 2, 7, 5, RGB(240, 236, 210));
                gfx_fill(x, y - 3, 3, 5, RGB(mix(8, 34, 100), mix(10, 28, 100), mix(30, 66, 100)));   /* Sichel */
            }
        }
    }

    /* Wolken - je mehr, desto bewoelkter */
    {
        int nc = !w.valid ? 2 : w.code == 0 ? 1 : w.code <= 2 ? 3 : 6;
        uint16_t cc = RGB(mix(60, grey ? 210 : 248, day), mix(62, grey ? 214 : 250, day), mix(90, grey ? 220 : 255, day));
        for (int i = 0; i < nc; i++) {
            int x = (int)((t / (5 + i * 3) + i * 41) % (GW + 40)) - 25, y = SCENE_TOP + 4 + (i * 11) % 28;
            draw_cloud(x, y, i % 3, cc);
        }
    }
    /* Gewitter: ab und zu ein Blitz */
    if (w.valid && w.code >= 95 && (t % 230) < 3) gfx_fill(0, SCENE_TOP, GW, SCENE_GROUND - SCENE_TOP, RGB(230, 230, 255));

hills:
    {
        /* zwei Huegelketten, hinten blasser */
        uint16_t far = RGB(mix(20, 120, day), mix(26, 160, day), mix(40, 140, day));
        uint16_t near = RGB(mix(16, 80, day), mix(30, 140, day), mix(30, 80, day));
        if (season(&lt) == WINTER) { far = RGB(mix(40, 200, day), mix(44, 210, day), mix(60, 225, day)); near = RGB(mix(50, 225, day), mix(54, 232, day), mix(70, 245, day)); }
        for (int x = 0; x < GW; x++) {
            int h1 = H1[x], h2 = H2[x];
            gfx_fill(x, h1, 1, SCENE_GROUND - h1, far);
            gfx_fill(x, h2, 1, SCENE_GROUND - h2, near);
        }
        /* der Baum links, je nach Jahreszeit */
        int se = season(&lt);
        uint16_t trunk = RGB(mix(30, 110, day), mix(22, 76, day), mix(20, 46, day));
        gfx_fill(STUMP_X - 1, 50, 3, SCENE_GROUND - 50, trunk);
        gfx_px(STUMP_X + 2, 56, trunk); gfx_px(STUMP_X + 3, 55, trunk);
        uint16_t leaf = se == SPRING ? RGB(mix(30, 110, day), mix(50, 190, day), mix(30, 90, day))
                      : se == SUMMER ? RGB(mix(20, 60, day), mix(50, 150, day), mix(25, 60, day))
                      : se == AUTUMN ? RGB(mix(50, 220, day), mix(30, 120, day), mix(15, 30, day))
                      : RGB(mix(40, 200, day), mix(44, 210, day), mix(60, 230, day));
        int sway = C_sway;
        gfx_fill(STUMP_X - 7 + sway, 42, 15, 9, leaf);
        gfx_fill(STUMP_X - 5 + sway, 38, 11, 5, leaf);
        gfx_fill(STUMP_X - 2 + sway, 36, 5, 3, leaf);
        if (se == SPRING) for (int i = 0; i < 8; i++) gfx_px(STUMP_X - 6 + (i * 5) % 13 + sway, 39 + (i * 3) % 10, RGB(255, 190, 210));
        if (se == AUTUMN) for (int i = 0; i < 6; i++) gfx_px(STUMP_X - 6 + (i * 7) % 13 + sway, 40 + (i * 5) % 10, RGB(200, 60, 30));
    }
    draw_particles(0);
}

/* ---------------------------------------------------------------- Vorn */

static void draw_particles(int front);

void scene_draw_front(void)
{
    if (theme) { draw_front_city(); draw_particles(1); return; }
    int day = C_day, se = C_se;
    int by = gfx_band_y0();

    if (by + GFX_BAND > SCENE_GROUND - 3) {
        /* Wiese: Verlauf, Grashalme wiegen sich im Wind */
        uint16_t g1 = se == WINTER ? RGB(mix(60, 235, day), mix(64, 240, day), mix(80, 250, day)) : RGB(mix(20, 90, day), mix(40, 170, day), mix(20, 60, day));
        uint16_t g2 = se == WINTER ? RGB(mix(50, 210, day), mix(54, 220, day), mix(70, 235, day)) : RGB(mix(15, 60, day), mix(30, 130, day), mix(15, 45, day));
        for (int y = SCENE_GROUND; y < GH; y++) gfx_fill(0, y, GW, 1, y < SCENE_GROUND + 6 ? g1 : g2);
        for (int x = 1; x < GW; x += 3) {
            int lean = C_lean[x];
            int h = 2 + (x * 7) % 3;
            uint16_t c = se == WINTER ? RGB(mix(90, 255, day), mix(90, 255, day), mix(110, 255, day)) : RGB(mix(30, 120, day), mix(60, 200, day), mix(30, 80, day));
            gfx_px(x + lean, SCENE_GROUND - h, c);
            gfx_px(x + (lean > 0), SCENE_GROUND - h + 1, c);
        }
        /* Blumen (nicht im Winter) */
        if (se != WINTER) {
            static const uint16_t FC[4] = { RGB(250, 90, 90), RGB(250, 220, 80), RGB(170, 120, 250), RGB(255, 255, 255) };
            for (int i = 0; i < 9; i++) {
                int x = 22 + (i * 23) % 66, y = SCENE_GROUND + 3 + (i * 5) % 9;
                int sw = C_flower[i];
                uint16_t c = FC[i % 4];
                if (day < 100) c = (uint16_t)((c >> 1) & 0x7BEF);
                gfx_px(x + sw, y - 2, c); gfx_px(x - 1 + sw, y - 1, c); gfx_px(x + 1 + sw, y - 1, c);
                gfx_px(x, y, RGB(mix(30, 80, day), mix(60, 150, day), mix(30, 60, day)));
            }
        }
        /* der gelbe Briefkasten - der Landeplatz der Eule */
        uint16_t post = RGB(mix(40, 110, day), mix(30, 80, day), mix(25, 50, day));
        gfx_fill(MAILBOX_X - 1, 66, 2, GH - 66, post);
        uint16_t yel = RGB(mix(90, 250, day), mix(70, 205, day), mix(10, 30, day)), dk = RGB(mix(40, 120, day), mix(30, 90, day), 10);
        gfx_fill(MAILBOX_X - 5, 59, 10, 8, yel);
        gfx_fill(MAILBOX_X - 4, 58, 8, 1, yel);
        gfx_fill(MAILBOX_X - 4, 62, 8, 1, dk);          /* Einwurfschlitz */
        gfx_px(MAILBOX_X - 1, 64, dk); gfx_px(MAILBOX_X, 64, dk); gfx_px(MAILBOX_X + 1, 65, dk);   /* Posthorn */
        /* Baumstumpf links (der zweite Landeplatz) */
        uint16_t st = RGB(mix(40, 130, day), mix(30, 90, day), mix(20, 55, day));
        gfx_fill(STUMP_X - 4 + 12, 69, 9, GH - 69, st);
        gfx_fill(STUMP_X - 3 + 12, 68, 7, 1, RGB(mix(60, 190, day), mix(45, 150, day), mix(30, 100, day)));
        /* Laterne der Nacht: der Briefkasten leuchtet nicht, aber Gluehwuermchen tun es */
    }
    draw_particles(1);
}

/* front = 0: alles am Himmel (liegt hinter den Textbausteinen); front = 1: nur Bodennahes */
static void draw_particles(int front)
{
    for (int i = 0; i < MAXS; i++) {
        sp_t *s = &S[i];
        if (s->life <= 0) continue;
        bool ground = s->kind == K_BUTTERFLY || s->kind == K_FIREFLY;   /* Glut steigt hoch: hinten */
        if (ground != (front != 0)) continue;
        int x = (int)s->x, y = (int)s->y;
        switch (s->kind) {
        case K_RAIN: gfx_px(x, y, s->c); gfx_px(x, y - 1, s->c); gfx_px(x + 1, y - 2, s->c); break;
        case K_BIRD: {
            int f = (t / 6 + i) & 1;
            gfx_px(x, y, s->c); gfx_px(x - 1, y - f, s->c); gfx_px(x + 1, y - f, s->c);
            gfx_px(x - 2, y - 1 + f, s->c); gfx_px(x + 2, y - 1 + f, s->c);
            break;
        }
        case K_BUTTERFLY: {
            int f = (t / 4) & 1;
            gfx_px(x, y, RGB(60, 40, 30));
            gfx_px(x - 1, y - f, s->c); gfx_px(x + 1, y - f, s->c);
            break;
        }
        case K_FIREFLY: if ((t / 10 + i) % 4) gfx_px(x, y, s->c); break;
        case K_DRONE:
            gfx_fill(x - 2, y, 5, 1, s->c); gfx_px(x, y - 1, s->c);
            gfx_px(x - 2, y - 1 + (int)((t / 3) & 1), s->c); gfx_px(x + 2, y - 1 + (int)((t / 3) & 1), s->c);
            if ((t / 10) & 1) gfx_px(x, y + 1, RGB(255, 50, 40));
            break;
        case K_EMBER: if ((t / 4 + i) % 3) gfx_px(x, y, s->life > 40 ? RGB(255, 170, 60) : RGB(200, 70, 30)); break;
        case K_SMOKE: if (s->life > 60 || ((x + y) & 1)) { gfx_px(x, y, s->c); gfx_px(x + 1, y, s->c); } break;
        default: gfx_px(x, y, s->c); break;
        }
    }
}

/* ---------------------------------------------------------------- Kopfzeile */

static void icon_weather(int x, int y, int code)
{
    uint16_t sun = RGB(255, 210, 60), cl = RGB(230, 232, 240), rn = RGB(120, 170, 255);
    if (code <= 1) { gfx_fill(x + 1, y, 3, 5, sun); gfx_fill(x, y + 1, 5, 3, sun); return; }
    gfx_fill(x, y + 1, 6, 3, cl);
    gfx_fill(x + 1, y, 3, 1, cl);
    if (code >= 51) { gfx_px(x + 1, y + 4, rn); gfx_px(x + 4, y + 4, rn); }
}

void scene_draw_topbar(void)
{
    if (gfx_band_y0() > SCENE_TOP) return;
    gfx_fill(0, 0, GW, SCENE_TOP, RGB(20, 18, 32));
    char b[32];
    text_simplify(b, 14, sources_city()[0] ? sources_city() : "...");
    gfx_text(2, 1, b, RGB(230, 220, 190));
    int city_end = 2 + gfx_text_w(b);
    weather_t w = C_w;
    int x = GW - 2;
    if (w.valid) {
        snprintf(b, sizeof b, "%.0fC", w.temp);
        x -= gfx_text_w(b);
        gfx_text(x, 1, b, RGB(255, 230, 150));
        x -= 8;
        icon_weather(x, 1, w.code);
        x -= 3;
    }
    if (net_time_valid()) {
        snprintf(b, sizeof b, "%02d:%02d", C_lt.tm_hour, C_lt.tm_min);
        x -= gfx_text_w(b);
        gfx_text(x, 1, b, C_WHITE);
    }
    /* Update-Hinweis: 5x6 Download-Symbol links neben der Uhrzeit (1 s an, 0,5 s aus),
     * nur wenn zwischen Ort und Uhrzeit noch Platz ist */
    int ax = x - 6;
    if (ota_update_available(NULL, 0) && ax > city_end + 1 && esp_timer_get_time() % 1500000 < 1000000) {
        static const char *const DL[6] = { "..X..", "..X..", "X.X.X", ".XXX.", "..X..", "XXXXX" };
        uint16_t g = RGB(120, 240, 140);
        for (int r = 0; r < 6; r++)
            for (int c = 0; c < 5; c++)
                if (DL[r][c] == 'X') gfx_px(ax + c, 1 + r, g);
    }
}

void scene_perch(int which, float *x, float *y)
{
    if (which == 0) { *x = MAILBOX_X; *y = 58; }      /* auf dem Briefkasten */
    else { *x = STUMP_X + 12; *y = 68; }              /* auf dem Baumstumpf */
}

/* Sitzplatz im Gras (x des Eulen-Fusspunkts, Sprite+Rand = x-9..x+8): frei von Pfosten,
 * Stumpf, Stamm, Briefkasten bzw. Laterne, Fass, Ladestation des aktuellen Themas.
 * Landschaft: Stamm 11..13, Stumpf 20..28, Briefkasten 91..100 -> frei 29..90.
 * Stadt: Laterne 12, Fass 20..28, Ladestation 91..106 -> frei 29..90. */
void scene_ground_perch(int which, float *x)
{
    *x = which == 0 ? 82.0f : 38.0f;   /* 73..90 rechts, 29..46 links */
}
