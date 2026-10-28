#include "courier.h"
#include "cards.h"
#include "gfx.h"
#include "owl_art.h"
#include "scene.h"
#include "sources.h"
#include "text.h"
#include "net.h"
#include "config.h"
#include "notify.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- Aufbau des Bildes
 *
 * Jeder Artikel wird von oben nach unten aufgebaut. Ist er laenger als der Bildschirm, laeuft er
 * als Foerderband weiter: ist die oberste Zeile gelesen, holt der Bote sie weg, die anderen gleiten
 * hoch, und er haengt unten die naechste an. Ist der Artikel komplett zu sehen, bekommt man Zeit
 * fuer den Rest - dann packt der Bote alle Zeilen auf einmal und fliegt mit dem Stapel davon.
 * Der naechste Artikel (oder eine Tafel) beginnt wieder oben auf leerem Schirm.
 */

#define ROWS      7
#define ROW_Y0    10
#define ROW_H     7
/* Grosse Tafel (bis y 62 inkl. Schatten) bleibt frei: Eule sitzt waehrend des Lesens unten im Gras.
 * Sprite 16 hoch, Zeichnung ab O.y-16, Umriss 1 px darueber -> Oberkante >= 63 bei O.y = GH (80);
 * Fuesse liegen auf der letzten Bildzeile. */
#define PANEL_PERCH_Y GH
#define LEFT      3
#define SPEED     4.5f
#define MAX_WRAP  24    /* Zeilen je Artikel hoechstens */
#define SETTLE    0.35f /* Anteil des Restwegs je Takt: gleitet weich an den Platz, ohne Nachfedern */

/* Lesetempo: so lange bleibt die oberste Zeile, bevor sie geholt wird */
#define READ_BASE_T   12    /* Takte Grundzeit */
#define READ_CHAR_T   1.4f  /* Takte je Zeichen (24 Zeichen ~ 1,5 s) */
#define FIRST_READ_T  45    /* zusaetzlich, wenn ein Bildschirm frisch voll ist */

#define C_WHITE   RGB(255, 255, 255)

static const uint16_t SRC_COLOR[SRC_COUNT] = {
    [SRC_HALLO] = RGB(150, 90, 200), [SRC_NEWS] = RGB(30, 90, 190), [SRC_WETTER] = RGB(40, 150, 220),
    [SRC_WIKI_HEUTE] = RGB(190, 130, 40), [SRC_WIKI_GEBURT] = RGB(210, 80, 140), [SRC_WIKI_ZUFALL] = RGB(110, 110, 130),
    [SRC_FAKT] = RGB(40, 160, 100), [SRC_JW] = RGB(90, 70, 170),
};

static int theme;   /* 0 = Eule in der Landschaft, 1 = Roboter in der zerfallenen Stadt */

/* ---------------------------------------------------------------- Zeilen */

typedef struct {
    char txt[CARD_W_MAX + 1];
    uint16_t fg;
    uint8_t src;
    bool head;
} line_t;

typedef struct {
    line_t l;
    int w;
    float x, y, ty, vx, vy;
    uint8_t st;
} block_t;
enum { B_NONE, B_CARRIED, B_SETTLE, B_PLACED, B_GONE, B_PENDING };   /* PENDING: abgeholt, Bote ist unterwegs */

static block_t R[ROWS];           /* die Zeilen auf dem Schirm, [0] = oben */
static int nrows;
static block_t GONE[4];           /* weggeworfene Zeilen, die noch aus dem Bild fliegen */
static block_t carried;           /* was der Bote gerade traegt */

#define QMAX 40
static line_t Q[QMAX];            /* Warteschlange der naechsten Zeilen */
static int qhead, qn;
static card_t pcard;              /* Tafel, die als naechstes dran ist */
static bool panel_waiting;
static char pending_mood[8];

/* Eine Tafel (Wetter, Fakt) */
static struct { bool on; float x, y, ty, vy; int w, h; bool carried; } T;

/* Der Bote. frame: Gesicht beim Sitzen (OWL_*); flying/flap: Fluegelschlag bzw. Duese beim Fliegen */
static struct { float x, y; int frame, face, perch, flap; bool flying; } O;

enum { F_FILL, F_WAIT, F_CYCLE, F_CLEAR, F_PANEL_IN, F_PANEL_READ, F_PANEL_OUT, F_IDLE, F_NOTE };
static float off_x[ROWS], off_y[ROWS];   /* Stapel beim Wegtragen: Lage der Zeilen zum Boten */
static int phase, stage, t, wait_t, blink_t, look_t, side = 1;
static uint32_t ticks, rng = 0x0E1E;
static char bubble[12];
static int bubble_t;

typedef struct { float x, y, vx, vy; uint16_t c; uint8_t life; } fx_t;
#define MAXFX 40
static fx_t FX[MAXFX];

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int rr(int a, int b) { return a + (int)(rnd() % (uint32_t)(b - a + 1)); }
static float rf(float a, float b) { return a + (b - a) * (rnd() & 0xFFFF) / 65535.0f; }

static void fx(float x, float y, float vx, float vy, uint16_t c, int life)
{
    for (int i = 0; i < MAXFX; i++) if (!FX[i].life) { FX[i] = (fx_t){ x, y, vx, vy, c, (uint8_t)life }; return; }
}

static void sparkle(float x, float y, uint16_t c, int n)
{
    for (int k = 0; k < n; k++) fx(x, y, rf(-0.8f, 0.8f), rf(-0.9f, 0.2f), c, rr(8, 16));
}

static void say(const char *s, int secs) { snprintf(bubble, sizeof bubble, "%s", s); bubble_t = secs * COURIER_HZ; }

/* ---------------------------------------------------------------- Bote bewegen */

static bool fly_to(float tx, float ty, float speed)
{
    float dx = tx - O.x, dy = ty - O.y, d = sqrtf(dx * dx + dy * dy);
    O.flying = true;
    O.flap = (int)((ticks / 3) % 4);
    if (fabsf(dx) > 0.5f) O.face = dx > 0 ? 1 : -1;
    if (d <= speed) { O.x = tx; O.y = ty; return true; }
    O.x += dx / d * speed;
    O.y += dy / d * speed + sinf(ticks * 0.6f) * 0.3f;
    if (rr(0, theme ? 6 : 60) == 0) {   /* Eule: mal eine Feder; Roboter: Funken aus der Duese */
        if (theme) fx(O.x + rf(-1, 1), O.y + 1, rf(-0.3f, 0.3f), rf(0.4f, 1.0f), RGB(255, 170, 60), 8);
        else fx(O.x, O.y - 6, rf(-0.4f, 0.4f), rf(0.1f, 0.4f), RGB(200, 160, 110), 40);
    }
    return false;
}

/* Sitzen bzw. stehen: blinzeln, umschauen */
static void idle_face(void)
{
    O.flying = false;
    O.frame = OWL_SITZT;
    if (--blink_t <= 0) blink_t = rr(60, 150);
    if (blink_t < 5) O.frame = OWL_BLINZELT;
    if (--look_t <= 0) look_t = rr(90, 240);
    if (look_t < 25) O.frame = look_t < 12 ? OWL_LINKS : OWL_RECHTS;
    if (bubble_t) O.frame = bubble[0] == '!' ? OWL_STAUNT : OWL_FROH;
}

/* ---------------------------------------------------------------- Warteschlange */

static int text_w(const line_t *l) { return (int)strlen(l->txt) * 4 - 1 + (l->head ? 8 : 7); }

static void q_push(const char *txt, uint16_t fg, int src, bool head)
{
    if (qn >= QMAX) return;
    line_t *l = &Q[(qhead + qn++) % QMAX];
    snprintf(l->txt, sizeof l->txt, "%.*s", CARD_W_MAX, txt);
    l->fg = fg; l->src = (uint8_t)src; l->head = head;
}

static bool q_pop(line_t *out)
{
    if (!qn) return false;
    *out = Q[qhead];
    qhead = (qhead + 1) % QMAX;
    qn--;
    return true;
}

/* Naechste Karte holen: Textkarten kommen ins Band, Tafeln warten auf ihren Auftritt */
static void refill(void)
{
    if (qn || panel_waiting) return;
    card_t c;
    if (!cards_next(&c)) return;
    if (c.kind != CARD_TEXT) { pcard = c; panel_waiting = true; return; }
    if (c.src == SRC_HALLO) {   /* die Begruessung spricht der Bote selbst - Eule oder Roboter */
        char b[96];
        card_begin(&c, CARD_TEXT, SRC_HALLO, "HALLO!");
        snprintf(b, sizeof b, theme ? "Dein Post-Robo ist in %s gelandet." : "Deine Posteule ist in %s gelandet.", sources_city());
        card_part(&c, b, C_WHITE);
        card_part(&c, theme ? "Ab jetzt liefere ich dir die Neuigkeiten." : "Ab jetzt bringe ich dir die Neuigkeiten.", RGB(205, 210, 222));
        snprintf(c.mood, sizeof c.mood, "%s", theme ? "PIEP!" : "HUHU!");
    }
    q_push(c.head, C_WHITE, c.src, true);
    static char tmp[MAX_WRAP][CARD_W_MAX + 2];
    for (int p = 0; p < c.nparts; p++) {
        char part[CARD_TEXT_MAX];
        memcpy(part, c.text + c.part[p].off, c.part[p].len);
        part[c.part[p].len] = 0;
        int k = text_wrap(part, CARD_W_MAX, &tmp[0][0], CARD_W_MAX + 2, MAX_WRAP);
        for (int i = 0; i < k && qn < QMAX; i++) q_push(tmp[i], c.part[p].color, c.src, false);
    }
    snprintf(pending_mood, sizeof pending_mood, "%s", c.mood);
}

static int read_time(const block_t *b) { return (READ_BASE_T + (int)(strlen(b->l.txt) * READ_CHAR_T)) * config_duration() / 20; }

/* ---------------------------------------------------------------- Ablauf */

/* Eine Zeile von der Seite holen und unten anhaengen. true, wenn fertig. */
static bool deliver(void)
{
    if (stage == 0) {
        float sx = side < 0 ? -14 : GW + 14;
        if (fly_to(sx, ROW_Y0 + nrows * ROW_H - 2, SPEED * 1.4f)) {
            carried.st = B_CARRIED;
            stage = 1;
        }
        return false;
    }
    float tx = LEFT + carried.w / 2.0f, ty = ROW_Y0 + nrows * ROW_H - 1;
    bool there = fly_to(tx, ty, SPEED);
    carried.x = O.x - carried.w / 2.0f; carried.y = O.y + 1;
    if (!there) return false;
    block_t *b = &R[nrows++];
    *b = carried;
    b->st = B_SETTLE; b->ty = ROW_Y0 + (nrows - 1) * ROW_H;
    carried.st = B_NONE;
    sparkle(b->x + b->w, b->y + 3, theme ? RGB(255, 190, 80) : RGB(255, 240, 180), 3);
    if (b->l.head && pending_mood[0]) { say(pending_mood, 2); pending_mood[0] = 0; }
    stage = 0;
    return true;
}

/* Die oberste Zeile packen und zur Seite wegtragen; die anderen ruecken hoch. true, wenn weg. */
static bool take_top(float speed)
{
    if (stage == 0) {
        block_t *b = &R[0];
        if (fly_to(b->x + b->w / 2.0f, b->y - 1, speed * 1.3f)) {
            carried = *b;
            carried.st = B_CARRIED;
            for (int i = 1; i < nrows; i++) { R[i - 1] = R[i]; R[i - 1].ty = ROW_Y0 + (i - 1) * ROW_H; R[i - 1].st = B_SETTLE; }
            nrows--;
            R[nrows].st = B_NONE;
            stage = 1;
            side = rr(0, 1) ? 1 : -1;
        }
        return false;
    }
    /* raus zur Seite, dort loslassen - die Zeile segelt davon */
    bool out = fly_to(side < 0 ? -16 : GW + 16, O.y - 2, speed);
    carried.x = O.x - carried.w / 2.0f; carried.y = O.y + 1;
    if (!out) return false;
    for (int i = 0; i < 4; i++) if (GONE[i].st == B_NONE) {
        GONE[i] = carried; GONE[i].st = B_GONE; GONE[i].vx = side * 2.5f; GONE[i].vy = -0.4f;
        break;
    }
    carried.st = B_NONE;
    stage = 0;
    return true;
}

/* Ist der Artikel auf dem Schirm komplett? Das ist er, wenn als naechstes ein neuer Kopf kommt,
 * eine Tafel wartet oder gar nichts mehr da ist. */
static bool article_done(void)
{
    refill();
    return qn ? Q[qhead].head : true;
}

static void start_wait(int extra)
{
    phase = F_WAIT; t = 0;
    if (nrows && article_done()) {
        /* Ende des Artikels: Zeit fuer alles, was noch auf dem Schirm steht */
        int chars = 0;
        for (int i = 0; i < nrows; i++) chars += (int)strlen(R[i].l.txt);
        wait_t = FIRST_READ_T + (int)(chars * READ_CHAR_T);
    } else {
        wait_t = (nrows ? read_time(&R[0]) : 20) + extra;
    }
}

static void tick_fill(void)
{
    if (stage == 0 && carried.st == B_NONE) {
        if (nrows >= ROWS) { start_wait(FIRST_READ_T); return; }
        refill();
        /* kurzer Artikel ist schon ganz da: nichts vom naechsten anhaengen */
        if (nrows && (!qn || Q[qhead].head)) { start_wait(0); return; }
        line_t l;
        if (!q_pop(&l)) {
            if (panel_waiting) { phase = F_PANEL_IN; stage = 0; t = 0; return; }
            phase = F_IDLE; t = 0;
            return;
        }
        memset(&carried, 0, sizeof carried);
        carried.l = l; carried.w = text_w(&l);
        carried.st = B_PENDING;
        side = -side;   /* abwechselnd von links und rechts holen */
    }
    deliver();
}

static void tick_wait(void)
{
    t++;
    /* in der Lesepause auf den Sitzplatz - nie ueber dem Text schweben */
    float px, py;
    scene_perch(O.perch, &px, &py);
    if (fly_to(px, py, SPEED)) idle_face();
    if (t >= wait_t) {
        /* fertig gelesen: ganzer Artikel weg - oder nur die oberste Zeile tauschen */
        phase = article_done() ? F_CLEAR : F_CYCLE;
        stage = 0; t = 0;
        O.perch = rr(0, 3) ? 0 : 1;
    }
}

static void tick_cycle(void)
{
    /* oberste Zeile weg, dann die naechste unten dran */
    if (stage < 2) {
        if (stage == 0 && !nrows) { phase = F_FILL; stage = 0; return; }
        if (take_top(SPEED)) stage = 2;
        return;
    }
    if (stage == 2) {
        line_t l;
        if (!q_pop(&l)) { start_wait(0); return; }
        memset(&carried, 0, sizeof carried);
        carried.l = l; carried.w = text_w(&l);
        stage = 3;
        carried.st = B_CARRIED;   /* er ist schon draussen: gleich zurueck mit der neuen Zeile */
        return;
    }
    float tx = LEFT + carried.w / 2.0f, ty = ROW_Y0 + nrows * ROW_H - 1;
    bool there = fly_to(tx, ty, SPEED);
    carried.x = O.x - carried.w / 2.0f; carried.y = O.y + 1;
    if (!there) return;
    block_t *b = &R[nrows++];
    *b = carried;
    b->st = B_SETTLE; b->ty = ROW_Y0 + (nrows - 1) * ROW_H;
    carried.st = B_NONE;
    sparkle(b->x + b->w, b->y + 3, theme ? RGB(255, 190, 80) : RGB(255, 240, 180), 3);
    start_wait(0);
}

/* Den ganzen Artikel auf einmal wegtragen: oben packen, der Stapel haengt am Boten */
static void tick_clear(void)
{
    if (!nrows) { phase = panel_waiting ? F_PANEL_IN : F_FILL; stage = 0; t = 0; return; }
    if (stage == 0) {
        if (fly_to(LEFT + 30, R[0].y - 1, SPEED * 1.2f)) {
            for (int i = 0; i < nrows; i++) { off_x[i] = R[i].x - O.x; off_y[i] = R[i].y - O.y; R[i].st = B_CARRIED; }
            side = rr(0, 1) ? 1 : -1;
            say(theme ? "LADE AUS" : "WEG DAMIT", 1);
            stage = 1;
        }
        return;
    }
    /* weit genug raus, dass auch die breiteste Zeile verschwindet */
    bool out = fly_to(side > 0 ? GW + 40 : -110, O.y - 4, SPEED * 1.2f);
    for (int i = 0; i < nrows; i++) {   /* die Zeilen schwingen leicht hinterher */
        float lag = 0.35f + i * 0.05f;
        R[i].x += (O.x + off_x[i] - R[i].x) * lag;
        R[i].y += (O.y + off_y[i] - R[i].y) * lag;
    }
    if (out) {
        for (int i = 0; i < nrows; i++) R[i].st = B_NONE;
        nrows = 0;
        phase = panel_waiting ? F_PANEL_IN : F_FILL; stage = 0; t = 0;
    }
}

static void tick_panel_in(void)
{
    if (stage == 0) {
        T.w = GW - 8; T.h = pcard.kind == CARD_WEATHER ? 50 : 52;
        T.x = 4; T.ty = ROW_Y0; T.y = -T.h - 20; T.vy = 0; T.on = true; T.carried = true;
        stage = 1;
    }
    if (stage == 1) {
        /* Tafel ausserhalb des Bildes anhaengen, sonst springt sie halb ins Bild */
        if (fly_to(T.x + T.w / 2, -T.h - 2, SPEED * 1.4f)) { stage = 2; T.carried = true; }
    } else if (stage == 2) {
        bool there = fly_to(T.x + T.w / 2, T.ty - 1, SPEED * 0.8f);
        T.y = O.y + 1;
        if (there) {
            T.carried = false; stage = 3; t = 0;
            sparkle(T.x + T.w / 2, T.y + T.h, RGB(255, 240, 180), 6);
            if (pcard.mood[0]) say(pcard.mood, 2);
        }
    } else {
        O.y -= 1.0f;
        if (++t > 4) { phase = F_PANEL_READ; stage = 0; t = 0; panel_waiting = false; }
    }
}

static void tick_panel_read(void)
{
    float px;
    scene_ground_perch(O.perch, &px);   /* im Gras, nie vor Pfosten/Stumpf; Seite bleibt erhalten */
    if (fly_to(px, PANEL_PERCH_Y, SPEED)) idle_face();
    if (++t > COURIER_HZ * 9) { phase = F_PANEL_OUT; stage = 0; t = 0; }
}

static void tick_panel_out(void)
{
    if (stage == 0) {
        if (fly_to(T.x + T.w / 2, T.y - 1, SPEED * 1.2f)) { stage = 1; T.carried = true; }
    } else {
        fly_to(T.x + T.w / 2, -T.h - 30, SPEED);
        T.y = O.y + 1;
        if (O.y < -T.h - 20) { T.on = false; phase = F_FILL; stage = 0; t = 0; }
    }
}

/* ---------------------------------------------------------------- Push-Nachricht
 * Unterbricht alles: der Bote bringt einen Brief als Tafel, liest ihn vor und
 * traegt ihn wieder weg. Danach geht es genau dort weiter, wo er war. */

#define NOTE_H    56
#define NOTE_VIS  7     /* Textzeilen, die auf die Tafel passen */
#define NOTE_MAX  20
#define NOTE_W    20    /* Zeichen je Zeile: rechts bleibt Platz fuer den Boten */
static struct {
    bool on, carried;
    int stage, t, hold, n, kind;
    float y, vy;
    char head[28], when[8];
    char lines[NOTE_MAX][CARD_W_MAX + 2];
    uint16_t color[NOTE_MAX];
} N;
static int saved_phase, saved_stage, saved_t;

void courier_note(const card_t *c, int kind)
{
    if (N.on) return;
    memset(&N, 0, sizeof N);
    snprintf(N.head, sizeof N.head, "%.20s", c->head);
    N.kind = kind;
    if (net_time_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(N.when, sizeof N.when, "%H:%M", &tm);
    }
    int chars = 0;
    for (int p = 0; p < c->nparts && N.n < NOTE_MAX; p++) {
        char part[CARD_TEXT_MAX];
        memcpy(part, c->text + c->part[p].off, c->part[p].len);
        part[c->part[p].len] = 0;
        int k = text_wrap(part, NOTE_W, &N.lines[N.n][0], CARD_W_MAX + 2, NOTE_MAX - N.n);
        for (int i = 0; i < k; i++) {
            N.color[N.n + i] = c->part[p].color;
            chars += (int)strlen(N.lines[N.n + i]);
        }
        N.n += k;
    }
    /* Lesezeit: 8 s Grundzeit plus ~15 Zeichen je Sekunde, hoechstens 40 s */
    N.hold = COURIER_HZ * 8 + chars * COURIER_HZ / 15;
    if (N.hold > COURIER_HZ * 40) N.hold = COURIER_HZ * 40;
    N.y = -NOTE_H - 20;
    N.on = true;
    saved_phase = phase; saved_stage = stage; saved_t = t;
    phase = F_NOTE; stage = 0; t = 0;
}

bool courier_note_active(void) { return N.on; }

static void tick_note(void)
{
    float cx = GW / 2.0f;
    N.t++;
    if (stage == 0) {               /* Brief oben abholen */
        if (fly_to(cx, -18, SPEED * 1.6f)) { stage = 1; N.carried = true; }
    } else if (stage == 1) {        /* herunterbringen */
        bool there = fly_to(cx, ROW_Y0 - 1, SPEED);
        N.y = O.y + 1;
        if (there) {
            N.carried = false; stage = 2; t = 0;
            sparkle(cx, N.y + NOTE_H, RGB(255, 220, 120), 8);
            say(theme ? "PING!" : "POST!", 2);
        }
    } else if (stage == 2) {        /* vorlesen lassen: unten rechts in der Tafel sitzen */
        if (fly_to(GW - 12, ROW_Y0 + NOTE_H - 1, SPEED)) { idle_face(); O.face = -1; }
        if (++t > N.hold) { stage = 3; t = 0; }
    } else if (stage == 3) {        /* wieder abholen */
        if (fly_to(cx, N.y - 1, SPEED * 1.2f)) { stage = 4; N.carried = true; }
    } else {                        /* und wegfliegen */
        fly_to(cx, -NOTE_H - 30, SPEED);
        N.y = O.y + 1;
        if (O.y < -NOTE_H - 20) {
            N.on = false;
            phase = saved_phase; stage = saved_stage; t = saved_t;
        }
    }
    if (!N.carried && stage >= 2) {  /* gleitet an seinen Platz */
        N.y += (ROW_Y0 - N.y) * SETTLE;
        if (fabsf(ROW_Y0 - N.y) < 0.3f) { N.y = ROW_Y0; N.vy = 0; }
    }
}

void courier_tick(void)
{
    ticks++;
    if (bubble_t) bubble_t--;
    scene_tick();
    switch (phase) {
    case F_FILL: tick_fill(); break;
    case F_WAIT: tick_wait(); break;
    case F_CYCLE: tick_cycle(); break;
    case F_CLEAR: tick_clear(); break;
    case F_PANEL_IN: tick_panel_in(); break;
    case F_PANEL_READ: tick_panel_read(); break;
    case F_PANEL_OUT: tick_panel_out(); break;
    case F_NOTE: tick_note(); break;
    case F_IDLE: {   /* noch nichts da: auf dem Sitzplatz warten */
        t++;
        float px, py;
        scene_perch(0, &px, &py);
        if (fly_to(px, py, SPEED)) idle_face();
        if (t % 180 == 20) say(net_connected() ? (theme ? "DATEN?" : "POST?") : "WLAN?", 2);
        if (t % 30 == 0) { phase = F_FILL; stage = 0; }
        break;
    }
    }
    /* Zeilen gleiten an ihren Platz, Weggeworfenes segelt davon */
    for (int i = 0; i < nrows; i++) {
        block_t *b = &R[i];
        if (b->st != B_SETTLE) continue;
        b->x += (LEFT - b->x) * 0.4f;
        b->y += (b->ty - b->y) * SETTLE;
        if (fabsf(b->ty - b->y) < 0.3f) { b->y = b->ty; b->x = LEFT; b->st = B_PLACED; }
    }
    for (int i = 0; i < 4; i++) if (GONE[i].st == B_GONE) {
        GONE[i].x += GONE[i].vx; GONE[i].y += GONE[i].vy; GONE[i].vy += 0.08f;
        if (GONE[i].x > GW + 8 || GONE[i].x + GONE[i].w < -8 || GONE[i].y > GH) GONE[i].st = B_NONE;
    }
    if (T.on && !T.carried && phase != F_PANEL_OUT) {
        T.y += (T.ty - T.y) * SETTLE;
        if (fabsf(T.ty - T.y) < 0.3f) { T.y = T.ty; T.vy = 0; }
    }
    for (int i = 0; i < MAXFX; i++) if (FX[i].life) {
        FX[i].life--; FX[i].x += FX[i].vx; FX[i].y += FX[i].vy; FX[i].vy += 0.03f;
    }
}

/* ---------------------------------------------------------------- Zeichnen */

static uint16_t block_bg(void) { return theme ? RGB(14, 18, 14) : RGB(24, 28, 48); }
static uint16_t block_edge(void) { return theme ? RGB(80, 66, 30) : RGB(10, 12, 24); }

static void rounded(int x, int y, int w, int h, uint16_t fill, uint16_t edge)
{
    gfx_fill(x + 1, y, w - 2, h, edge);
    gfx_fill(x, y + 1, w, h - 2, edge);
    gfx_fill(x + 1, y + 1, w - 2, h - 2, fill);
}

static void draw_block(const block_t *b)
{
    if (b->st == B_NONE) return;
    int x = (int)b->x, y = (int)b->y;
    uint16_t sc = SRC_COLOR[b->l.src < SRC_COUNT ? b->l.src : 0];
    gfx_fill(x + 1, y + 7, b->w - 1, 1, RGB(10, 10, 20));   /* Schatten */
    if (b->l.head) {
        rounded(x, y, b->w, 7, sc, block_edge());
        gfx_text(x + 4, y + 1, b->l.txt, C_WHITE);
    } else {
        rounded(x, y, b->w, 7, block_bg(), block_edge());
        gfx_fill(x + 1, y + 1, 2, 5, sc);                    /* Farbstreifen der Quelle */
        uint16_t fg = b->l.fg;
        if (theme && fg == RGB(205, 210, 222)) fg = RGB(150, 230, 140);   /* Roboter: Terminalgruen */
        gfx_text_plain(x + 5, y + 1, b->l.txt, fg);
    }
}

/* Grosse Symbole fuer Tafeln, etwa 18x16 */
static void big_icon(int icon, int x, int y, int code)
{
    uint16_t sun = RGB(255, 205, 50), sun2 = RGB(255, 245, 170), cl = RGB(225, 230, 240), cl2 = RGB(170, 178, 196);
    switch (icon) {
    case ICON_SUN: case 100:
        gfx_fill(x + 5, y + 4, 8, 8, sun); gfx_fill(x + 4, y + 5, 10, 6, sun); gfx_fill(x + 7, y + 6, 4, 4, sun2);
        for (int a = 0; a < 1024; a += 128) {
            int rx = x + 9 + 8 * icos(a + (int)ticks * 2) / 4096, ry = y + 8 + 8 * isin(a + (int)ticks * 2) / 4096;
            gfx_px(rx, ry, sun);
        }
        break;
    case 101: case 102: case 103: case 104: case 105:
        if (icon == 101 && code <= 2) { gfx_fill(x + 9, y + 1, 7, 7, sun); gfx_fill(x + 8, y + 2, 9, 5, sun); }
        gfx_fill(x + 2, y + 7, 15, 6, cl); gfx_fill(x + 5, y + 4, 7, 4, cl); gfx_fill(x + 10, y + 5, 5, 3, cl);
        gfx_fill(x + 3, y + 12, 13, 1, cl2);
        if (icon == 102) for (int i = 0; i < 4; i++) { int dy = (int)((ticks / 2 + i * 3) % 5); gfx_px(x + 4 + i * 3, y + 14 + dy, RGB(110, 170, 255)); gfx_px(x + 4 + i * 3, y + 15 + dy, RGB(110, 170, 255)); }
        if (icon == 103) for (int i = 0; i < 4; i++) gfx_px(x + 4 + i * 3, y + 14 + (int)((ticks / 6 + i * 2) % 4), C_WHITE);
        if (icon == 104) { gfx_line(x + 10, y + 13, x + 7, y + 17, sun); gfx_line(x + 7, y + 17, x + 11, y + 17, sun); gfx_line(x + 11, y + 17, x + 8, y + 21, sun); }
        if (icon == 105) for (int i = 0; i < 3; i++) gfx_fill(x + 1 + i, y + 14 + i * 2, 16, 1, cl2);
        break;
    case ICON_MOON:
        gfx_fill(x + 4, y + 2, 10, 12, RGB(240, 236, 210)); gfx_fill(x + 3, y + 4, 12, 8, RGB(240, 236, 210));
        gfx_fill(x + 9, y + 1, 7, 11, block_bg()); gfx_px(x + 6, y + 6, RGB(200, 196, 170)); gfx_px(x + 5, y + 10, RGB(200, 196, 170));
        break;
    case ICON_LEAF:
        for (int i = 0; i < 12; i++) gfx_fill(x + 3 + i, y + 12 - i, 3 + (i < 6 ? i / 2 : (11 - i) / 2), 2, RGB(90, 200, 90));
        gfx_line(x + 3, y + 15, x + 14, y + 3, RGB(40, 120, 50));
        break;
    case ICON_AIR:
        for (int i = 0; i < 3; i++) {
            int yy = y + 4 + i * 4, len = 12 - i * 3;
            gfx_fill(x + 2, yy, len, 1, RGB(170, 220, 255));
            gfx_px(x + 2 + len, yy - 1, RGB(170, 220, 255)); gfx_px(x + 3 + len, yy - 2, RGB(170, 220, 255));
        }
        break;
    case ICON_QUAKE:
        for (int i = 0; i < 16; i++) { int a = (i % 4 == 1) ? -5 : (i % 4 == 3) ? 5 : 0; if (i > 4 && i < 12) a *= 2; gfx_px(x + 1 + i, y + 8 + a / 2, RGB(240, 140, 60)); }
        gfx_fill(x + 1, y + 15, 16, 1, RGB(140, 100, 70));
        break;
    case ICON_PARTY:
        gfx_fill(x + 4, y + 1, 8, 9, RGB(240, 70, 80)); gfx_fill(x + 3, y + 3, 10, 5, RGB(240, 70, 80)); gfx_px(x + 6, y + 3, RGB(255, 200, 200));
        gfx_line(x + 8, y + 10, x + 7, y + 16, RGB(220, 220, 220));
        gfx_px(x + 14, y + 4, RGB(255, 220, 80)); gfx_px(x + 1, y + 12, RGB(120, 200, 255)); gfx_px(x + 13, y + 13, RGB(150, 255, 150));
        break;
    case ICON_CAL:
        gfx_fill(x + 2, y + 2, 14, 13, RGB(245, 245, 245)); gfx_fill(x + 2, y + 2, 14, 4, RGB(220, 60, 60));
        for (int i = 0; i < 9; i++) gfx_px(x + 4 + (i % 3) * 4, y + 8 + (i / 3) * 2, RGB(80, 80, 90));
        break;
    case ICON_ROCKET:
        gfx_fill(x + 7, y + 1, 4, 11, RGB(235, 235, 240)); gfx_px(x + 8, y, RGB(235, 235, 240)); gfx_px(x + 9, y, RGB(235, 235, 240));
        gfx_px(x + 8, y + 4, RGB(90, 160, 255)); gfx_fill(x + 5, y + 9, 2, 4, RGB(220, 60, 60)); gfx_fill(x + 11, y + 9, 2, 4, RGB(220, 60, 60));
        gfx_fill(x + 8, y + 12 + (int)(ticks & 1), 2, 3, RGB(255, 170, 40));
        break;
    case ICON_COIN:
        gfx_fill(x + 4, y + 2, 10, 12, RGB(250, 200, 50)); gfx_fill(x + 3, y + 4, 12, 8, RGB(250, 200, 50));
        gfx_fill(x + 6, y + 5, 6, 1, RGB(180, 120, 20)); gfx_fill(x + 6, y + 8, 5, 1, RGB(180, 120, 20)); gfx_fill(x + 6, y + 11, 6, 1, RGB(180, 120, 20));
        gfx_fill(x + 6, y + 5, 1, 7, RGB(180, 120, 20));
        break;
    }
}

static int wx_icon(int code)
{
    if (code <= 1) return 100;
    if (code == 45 || code == 48) return 105;
    if (code >= 95) return 104;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 103;
    if (code >= 51) return 102;
    return 101;
}

static const char *wx_word(int c)
{
    if (c == 0) return "KLAR";
    if (c <= 2) return "HEITER";
    if (c == 3) return "BEDECKT";
    if (c <= 48) return "NEBEL";
    if (c <= 57) return "NIESELN";
    if (c <= 67) return "REGEN";
    if (c <= 77) return "SCHNEE";
    if (c <= 82) return "SCHAUER";
    if (c <= 86) return "SCHNEESCHAUER";
    return "GEWITTER";
}

static void draw_panel(void)
{
    if (!T.on) return;
    int x = (int)T.x, y = (int)T.y, w = T.w, h = T.h;
    uint16_t sc = SRC_COLOR[pcard.src < SRC_COUNT ? pcard.src : 0];
    gfx_fill(x + 2, y + h, w - 2, 1, RGB(10, 10, 20));
    rounded(x, y, w, h, block_bg(), block_edge());
    gfx_fill(x + 1, y + 1, w - 2, 8, sc);
    gfx_text(x + 4, y + 2, pcard.head, C_WHITE);
    char b[40];
    if (pcard.kind == CARD_WEATHER) {
        weather_t wx;
        sources_weather(&wx);
        big_icon(wx_icon(wx.code), x + 4, y + 12, wx.code);
        snprintf(b, sizeof b, "%.0fC", wx.temp);
        gfx_text_big(x + 27, y + 12, b, RGB(255, 230, 150), 2);
        gfx_text(x + 27, y + 25, wx_word(wx.code), C_WHITE);
        snprintf(b, sizeof b, "WIND %.0f KM/H", wx.wind);
        gfx_text(x + 27, y + 31, b, RGB(180, 190, 210));
        for (int d = 0; d < 2; d++) {
            int cx = x + 4 + d * 50, cy = y + 38;
            gfx_text(cx, cy, d ? "MORGEN" : "HEUTE", RGB(150, 200, 255));
            snprintf(b, sizeof b, "%.0f-%.0fC", wx.tmin[d], wx.tmax[d]);
            gfx_text(cx, cy + 6, b, C_WHITE);
            int bar = wx.rain[d] * 16 / 100;
            gfx_fill(cx + 30, cy + 7, 17, 3, RGB(40, 46, 70));
            gfx_fill(cx + 30, cy + 7, bar, 3, RGB(110, 170, 255));
            snprintf(b, sizeof b, "%d%%", wx.rain[d]);
            gfx_text(cx + 30, cy, b, RGB(110, 170, 255));
        }
    } else {
        big_icon(pcard.icon, x + 4, y + 12, 0);
        gfx_text_big(x + 27, y + 12, pcard.big, RGB(255, 230, 150), 2);
        char tmp[4][CARD_W_MAX + 2];
        int row = 0;
        for (int p = 0; p < pcard.nparts && row < 4; p++) {
            char part[CARD_TEXT_MAX];
            memcpy(part, pcard.text + pcard.part[p].off, pcard.part[p].len);
            part[pcard.part[p].len] = 0;
            int k = text_wrap(part, CARD_W_MAX, &tmp[0][0], CARD_W_MAX + 2, 4 - row);
            for (int i = 0; i < k; i++, row++) gfx_text(x + 4, y + 28 + row * 6, tmp[i], pcard.part[p].color);
        }
    }
}

/* kleines Symbol (7x6) fuer die Art der Nachricht */
static void note_icon(int x, int y)
{
    uint16_t c = RGB(30, 20, 10);
    switch (N.kind) {
    case NK_CHAT:
        gfx_fill(x, y, 7, 4, c); gfx_px(x + 1, y + 4, c); gfx_px(x, y + 5, c);
        for (int i = 0; i < 3; i++) gfx_px(x + 1 + i * 2, y + 1 + ((ticks / 6) % 3 == (unsigned)i ? 0 : 1), RGB(255, 220, 120));
        break;
    case NK_MAIL:
        gfx_fill(x, y, 7, 5, c); gfx_line(x, y, x + 3, y + 2, RGB(255, 220, 120)); gfx_line(x + 3, y + 2, x + 6, y, RGB(255, 220, 120));
        break;
    case NK_CALL: {
        int d = (ticks % 30 < 15) ? (int)((ticks / 2) & 1) : 0;
        gfx_fill(x + d, y, 3, 1, c); gfx_fill(x + d, y, 1, 6, c); gfx_fill(x + d, y + 5, 3, 1, c);
        if ((ticks / 8) & 1) { gfx_px(x + 4, y + 2, c); gfx_px(x + 4, y + 3, c); gfx_px(x + 6, y + 1, c); gfx_px(x + 6, y + 4, c); }
        break;
    }
    default: {
        int s = (ticks / 8) & 1;
        gfx_fill(x + 2 + s, y, 3, 1, c); gfx_fill(x + 1 + s, y + 1, 5, 3, c); gfx_fill(x + s, y + 4, 7, 1, c); gfx_px(x + 3 - s, y + 5, c);
        break;
    }
    }
}

static void draw_note(void)
{
    if (!N.on) return;
    int x = 4, y = (int)N.y, w = GW - 8, h = NOTE_H;
    if (y + h + 1 < gfx_band_y0() || y >= gfx_band_y0() + GFX_BAND) return;
    gfx_fill(x + 2, y + h, w - 2, 1, RGB(10, 10, 20));
    rounded(x, y, w, h, block_bg(), RGB(230, 150, 30));
    bool flash = N.t < COURIER_HZ * 2 && ((N.t / 8) & 1);
    gfx_fill(x + 1, y + 1, w - 2, 8, flash ? RGB(255, 225, 130) : RGB(235, 150, 35));
    note_icon(x + 3, y + 2);
    gfx_text(x + 13, y + 2, N.head, C_WHITE);
    if (N.when[0]) gfx_text(x + w - 3 - gfx_text_w(N.when), y + 2, N.when, C_WHITE);
    /* langsam hochscrollen, wenn mehr Zeilen als Platz sind: erst 3 s stehen, dann eine Zeile je 1,5 s */
    int extra = N.n - NOTE_VIS, soff = 0;
    if (extra > 0 && stage >= 2) {
        int run = (stage == 2 ? t : N.hold) - COURIER_HZ * 3;
        soff = run > 0 ? run * 6 / (COURIER_HZ * 3 / 2) : 0;
        if (soff > extra * 6) soff = extra * 6;
    }
    for (int i = 0; i < N.n; i++) {
        int ly = y + 12 + i * 6 - soff;
        if (ly < y + 10 || ly + 5 > y + h - 2) continue;
        uint16_t fg = N.color[i];
        if (theme && fg == C_WHITE) fg = RGB(150, 230, 140);   /* Roboter: Terminalgruen */
        gfx_text(x + 4, ly, N.lines[i], fg);
    }
}

static void draw_courier(void)
{
    static const int FLY[4] = { OWL_FLUG_HOCH, OWL_FLUG_MITTE, OWL_FLUG_TIEF, OWL_FLUG_MITTE };
    int f = O.flying ? FLY[O.flap & 3] : O.frame;
    if (f < 0 || f >= OWL_COUNT) f = OWL_SITZT;
    const spr_t *s = theme ? &robot[f] : &owl[f];
    gfx_spr_outline(s, (int)O.x - 8, (int)O.y - 16, O.face < 0 ? SPR_FLIP : 0);
}

static void draw_bubble(void)
{
    if (!bubble_t || !bubble[0]) return;
    uint16_t edge = theme ? RGB(120, 230, 120) : RGB(10, 12, 24);
    int w = gfx_text_w(bubble) + 6, x = (int)O.x - w / 2, y = (int)O.y - 28;
    if (x < 1) x = 1;
    if (x + w > GW - 1) x = GW - 1 - w;
    if (y < SCENE_TOP + 1) y = SCENE_TOP + 1;
    int tx = (int)O.x, ty = y + 9;                      /* Spitze unter der Blase, zeigt zur Eule */
    /* Sitzt die Eule waehrend einer Tafel unten im Gras (ganz unter der Tafel), laege die
     * Blase ueber der Tafel: dann neben die Eule setzen, auf ihrer Hoehe, rechts wenn Platz
     * ist, sonst links (Spitze seitlich). Die Blase haengt immer an der Eule und fliegt mit;
     * solange die Eule noch ueber der Tafel fliegt, bleibt sie normal ueber ihr. */
    if (T.on && (int)O.y - 16 > (int)T.y + T.h && x < T.x + T.w && x + w > T.x &&
        y < T.y + T.h + 1 && y + 9 > T.y) {
        int right = (int)O.x + 10, left = (int)O.x - 10 - w;
        bool go_right = right + w <= GW - 1 || left < 1;
        x = go_right ? right : left;
        if (x + w > GW - 1) x = GW - 1 - w;
        if (x < 1) x = 1;
        y = (int)O.y - 13;                              /* auf Kopfhoehe der Eule */
        if (y < (int)T.y + T.h + 2) y = (int)T.y + T.h + 2;
        if (y > GH - 10) y = GH - 10;
        ty = y + 4;
        tx = go_right ? x - 1 : x + w;
    }
    rounded(x, y, w, 9, theme ? RGB(20, 30, 20) : C_WHITE, edge);
    gfx_px(tx, ty, edge);
    gfx_text_plain(x + 3, y + 2, bubble, theme ? RGB(140, 255, 140) : RGB(10, 12, 24));
}

void courier_render(void)
{
    for (int y = 0; y < GH; y += GFX_BAND) {
        gfx_band_begin(y);
        scene_draw_back();
        for (int i = 0; i < nrows; i++) draw_block(&R[i]);
        for (int i = 0; i < 4; i++) draw_block(&GONE[i]);
        draw_panel();
        scene_draw_front();
        if (carried.st == B_CARRIED && phase != F_NOTE) draw_block(&carried);
        draw_note();
        draw_courier();
        for (int i = 0; i < MAXFX; i++) if (FX[i].life) gfx_px((int)FX[i].x, (int)FX[i].y, FX[i].c);
        draw_bubble();
        scene_draw_topbar();
        gfx_band_end();
    }
}

void courier_init(void)
{
    O.x = GW + 14; O.y = 30; O.face = -1;
    phase = F_FILL;
    blink_t = 60; look_t = 120;
}

void courier_set_theme(int th) { theme = th; scene_set_theme(th); }
int courier_theme(void) { return theme; }

void courier_skip(void)
{
    if (phase == F_WAIT) t = wait_t;
    else if (phase == F_PANEL_READ) t = COURIER_HZ * 9;
}

void courier_status(char *buf, int n)
{
    static const char *const PHN[] = { "baut auf", "liest", "tauscht", "raeumt ab", "tafel rein", "tafel", "tafel raus", "wartet", "nachricht" };
    snprintf(buf, n, "%s: %s zeilen=%d warteschlange=%d | quellen: %s | news=%d wetter=%d heute=%d geburt=%d zufall=%d fakt=%d",
             theme ? "roboter" : "eule", PHN[phase], nrows, qn, sources_status(),
             cards_count(SRC_NEWS), cards_count(SRC_WETTER), cards_count(SRC_WIKI_HEUTE), cards_count(SRC_WIKI_GEBURT),
             cards_count(SRC_WIKI_ZUFALL), cards_count(SRC_FAKT));
}

void courier_reset(void) {
 memset(R,0,sizeof R);memset(GONE,0,sizeof GONE);memset(&T,0,sizeof T);memset(&carried,0,sizeof carried);
 nrows=qn=qhead=0;panel_waiting=false;phase=F_FILL;t=0;memset(&N,0,sizeof N);courier_init();
}
