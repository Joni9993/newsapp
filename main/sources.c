#include "sources.h"
#include "cards.h"
#include "json.h"
#include "rss.h"
#include "net.h"
#include "text.h"
#include "gfx.h"
#include "config.h"
#include "modules.h"
#include "calendar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "quellen";

/* Schriftfarben der Textbausteine (helle Schrift auf dunklen Bausteinen) */
#define T_TITLE  RGB(255, 255, 255)
#define T_BODY   RGB(205, 210, 222)
#define T_BLUE   RGB(130, 190, 255)
#define T_GOLD   RGB(255, 205, 90)
#define T_GREEN  RGB(140, 230, 150)

static char city[32], state_code[8] = "DE-BW";
static float lat = 49.18f, lon = 9.44f;
static int region_id = 1;   /* Tagesschau-Regionen: 1 = Baden-Wuerttemberg */
static weather_t weather;
static const char *status = "start";

const char *sources_city(void) { return city; }
const char *sources_status(void) { return status; }
void sources_weather(weather_t *out) { *out = weather; }

static bool fetch_failed;
/* Pause (z. B. waehrend eines Firmware-Updates): kein neuer Abruf, bis wieder frei */
static volatile bool paused, fetching;
void sources_pause(bool on) { paused = on; }
bool sources_idle(void) { return !fetching; }
static int source_get(const char *url,net_data_cb cb,void *ctx) { int code=net_get(url,cb,ctx); if(code!=200){fetch_failed=true;ESP_LOGW(TAG,"Abruf %d (heap %u): %.90s",code,(unsigned)esp_get_free_heap_size(),url);} return code; }
static bool feed_json(const char *d, size_t n, void *ctx) { return json_feed((json_t *)ctx, d, n); }

static void copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

static struct tm today(void)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    return lt;
}

/* ---------------------------------------------------------------- Ort */

/* Bundeslaender: Name -> Kuerzel fuer Feiertage, Nummer fuer Tagesschau-Regionalnachrichten */
static const struct { const char *name, *code; int region; } STATES[] = {
    { "Baden", "DE-BW", 1 }, { "Bayern", "DE-BY", 2 }, { "Berlin", "DE-BE", 3 }, { "Brandenburg", "DE-BB", 4 },
    { "Bremen", "DE-HB", 5 }, { "Hamburg", "DE-HH", 6 }, { "Hessen", "DE-HE", 7 }, { "Mecklenburg", "DE-MV", 8 },
    { "Niedersachsen", "DE-NI", 9 }, { "Nordrhein", "DE-NW", 10 }, { "Rheinland", "DE-RP", 11 },
    { "Saarland", "DE-SL", 12 }, { "Sachsen-Anhalt", "DE-ST", 14 }, { "Sachsen", "DE-SN", 13 },
    { "Schleswig", "DE-SH", 15 }, { "Th", "DE-TH", 16 },
};

static void set_state(const char *name)
{
    for (size_t i = 0; i < sizeof STATES / sizeof *STATES; i++)
        if (!strncmp(name, STATES[i].name, strlen(STATES[i].name))) {
            copy(state_code, sizeof state_code, STATES[i].code);
            region_id = STATES[i].region;
            return;
        }
}

static bool geo_cb(const char *k, const char *v, int depth, void *ctx)
{
    /* geocoding: {"results":[{"name":..,"latitude":..,"admin1":..}]} - Tiefe 3. ip-api: Tiefe 1. */
    int want = *(int *)ctx;
    if (depth != want) return true;
    if (!strcmp(k, "name") || !strcmp(k, "city")) { if (!city[0]) copy(city, sizeof city, v); }
    else if (!strcmp(k, "latitude") || !strcmp(k, "lat")) lat = strtof(v, NULL);
    else if (!strcmp(k, "longitude") || !strcmp(k, "lon")) lon = strtof(v, NULL);
    else if (!strcmp(k, "admin1") || !strcmp(k, "regionName")) set_state(v);
    return true;
}

static bool fetch_location(void)
{
 char wanted[64];uint8_t region;config_location(wanted,sizeof wanted,&region);region_id=region;snprintf(state_code,sizeof state_code,"%s",STATES[region_id-1].code);
 city[0]=0;if(!wanted[0])return false;
 char url[360],q[192];size_t o=0;
 for(const unsigned char *p=(const unsigned char*)wanted;*p && o+4<sizeof q;p++) { if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'))q[o++]=*p;else{o+=snprintf(q+o,sizeof q-o,"%%%02X",*p);} }q[o]=0;
 snprintf(url,sizeof url,"https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=de&countryCode=DE",q);
 int depth=3;json_t j;json_init(&j,geo_cb,&depth);int code=source_get(url,feed_json,&j);
 region_id=region;for(size_t i=0;i<sizeof STATES/sizeof *STATES;i++)if(STATES[i].region==region_id)snprintf(state_code,sizeof state_code,"%s",STATES[i].code);
 return code==200&&city[0];
}

/* ---------------------------------------------------------------- Wetter */

static int hhmm(const char *iso)   /* "2026-10-03T07:24" -> Minuten seit Mitternacht */
{
    const char *t = strchr(iso, 'T');
    return t ? atoi(t + 1) * 60 + atoi(t + 4) : 0;
}

static bool wx_cb(const char *k, const char *v, int depth, void *ctx)
{
    weather_t *w = ctx;
    static int i_max, i_min, i_rain, i_code;
    if (depth == 1 && !strcmp(k, "latitude")) i_max = i_min = i_rain = i_code = 0;
    /* "current":{...} auf Tiefe 2, die Tageslisten "daily":{"x":[..]} auf Tiefe 3 */
    if (depth == 2 && !strcmp(k, "temperature_2m")) w->temp = strtof(v, NULL);
    else if (depth == 2 && !strcmp(k, "weather_code")) w->code = atoi(v);
    else if (depth == 2 && !strcmp(k, "wind_speed_10m")) w->wind = strtof(v, NULL);
    else if (depth == 2 && !strcmp(k, "is_day")) w->is_day = atoi(v) != 0;
    else if (depth == 3 && !strcmp(k, "temperature_2m_max") && i_max < 2) w->tmax[i_max++] = strtof(v, NULL);
    else if (depth == 3 && !strcmp(k, "temperature_2m_min") && i_min < 2) w->tmin[i_min++] = strtof(v, NULL);
    else if (depth == 3 && !strcmp(k, "precipitation_probability_max") && i_rain < 2) w->rain[i_rain++] = atoi(v);
    else if (depth == 3 && !strcmp(k, "weather_code") && i_code < 2) w->code_day[i_code++] = atoi(v);
    else if (depth == 3 && !strcmp(k, "sunrise") && !w->sunrise_min) w->sunrise_min = hhmm(v);
    else if (depth == 3 && !strcmp(k, "sunset") && !w->sunset_min) w->sunset_min = hhmm(v);
    return true;
}

static void fetch_weather(void)
{
    status = "wetter";
    char url[360];
    snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
             "&current=temperature_2m,weather_code,wind_speed_10m,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
             "&timezone=auto&forecast_days=2", lat, lon);
    static weather_t w;
    memset(&w, 0, sizeof w);
    json_t j;
    json_init(&j, wx_cb, &w);
    if (source_get(url, feed_json, &j) != 200) return;
    w.valid = true;
    weather = w;

    card_t c;
    char head[40];
    snprintf(head, sizeof head, "WETTER %.20s", city);
    card_begin(&c, CARD_WEATHER, SRC_WETTER, head);
    strcpy(c.mood, w.code >= 51 ? "BRR!" : w.code <= 1 && w.is_day ? "SONNE!" : w.temp < 5 ? "KALT!" : "HM.");
    card_finish(&c);
    cards_set(SRC_WETTER, &c, 1);
    ESP_LOGI(TAG, "Wetter: %.1f Grad, Code %d, Sonne %d-%d", w.temp, w.code, w.sunrise_min, w.sunset_min);
}

/* ---------------------------------------------------------------- Tagesschau */

#define TOP_MAX 9
#define REG_MAX 6
typedef struct { char top[100], title[200], first[300], details[160], id[40]; int region; } story_t;
typedef struct { story_t top[TOP_MAX], reg[REG_MAX], cur; int ntop, nreg; json_t *j; } home_t;

static bool home_cb(const char *k, const char *v, int depth, void *ctx)
{
    home_t *s = ctx;
    if (depth != 3) return true;   /* {"news":[ {Meldung} ], "regional":[ {Meldung} ]} */
    story_t *c = &s->cur;
    if (!strcmp(k, "sophoraId")) { memset(c, 0, sizeof *c); copy(c->id, sizeof c->id, v); }
    else if (!strcmp(k, "topline")) copy(c->top, sizeof c->top, v);
    else if (!strcmp(k, "title")) copy(c->title, sizeof c->title, v);
    else if (!strcmp(k, "firstSentence")) copy(c->first, sizeof c->first, v);
    else if (!strcmp(k, "details")) copy(c->details, sizeof c->details, v);
    else if (!strcmp(k, "regionId")) c->region = atoi(v);
    else if (!strcmp(k, "type")) {   /* steht am Ende einer Meldung: nur Artikel, keine Videos */
        if (strcmp(v, "story") || !c->title[0]) return true;
        bool regional = !strcmp(s->j->ckey[1], "regional");
        if (!regional && s->ntop < TOP_MAX) s->top[s->ntop++] = *c;
        else if (regional && c->region == region_id && s->nreg < REG_MAX) s->reg[s->nreg++] = *c;
    }
    return true;
}

/* Artikeltext: {"content":[{"value":"<p>..</p>","type":"text"}]} - die ersten Absaetze */
typedef struct { char text[520], val[700]; int len; } detail_t;

static void strip_html(char *s)
{
    char *o = s;
    bool tag = false;
    for (; *s; s++) {
        if (*s == '<') tag = true;
        else if (*s == '>') tag = false;
        else if (!tag) *o++ = *s;
    }
    *o = 0;
}

static bool detail_cb(const char *k, const char *v, int depth, void *ctx)
{
    detail_t *d = ctx;
    if (depth != 3) return true;
    if (!strcmp(k, "value")) copy(d->val, sizeof d->val, v);
    else if (!strcmp(k, "type")) {
        if (!strcmp(v, "text") && d->val[0]) {
            strip_html(d->val);
            int room = (int)sizeof d->text - d->len - 2;
            if (room > 20) d->len += snprintf(d->text + d->len, room, "%s%s", d->len ? " " : "", d->val);
        }
        d->val[0] = 0;
        if (d->len > 420) return false;   /* genug fuer drei Seiten */
    }
    return true;
}

static void news_card(card_t *c, const story_t *s, const char *head, const char *body)
{
    card_begin(c, CARD_TEXT, SRC_NEWS, head);
    card_part(c, s->top, T_BLUE);
    card_part(c, s->title, T_TITLE);
    card_part(c, body && body[0] ? body : s->first, T_BODY);
    strcpy(c->mood, "!");
    card_finish(c);
}

typedef struct { card_t *list; int n, cap; char ids[40][40]; int nids; story_t cur; } more_t;

static bool more_cb(const char *k, const char *v, int depth, void *ctx)
{
    more_t *m = ctx;
    if (depth != 3) return true;   /* {"news":[ {Meldung} ]} */
    story_t *c = &m->cur;
    if (!strcmp(k, "sophoraId")) { memset(c, 0, sizeof *c); copy(c->id, sizeof c->id, v); }
    else if (!strcmp(k, "topline")) copy(c->top, sizeof c->top, v);
    else if (!strcmp(k, "title")) copy(c->title, sizeof c->title, v);
    else if (!strcmp(k, "firstSentence")) copy(c->first, sizeof c->first, v);
    else if (!strcmp(k, "regionId")) c->region = atoi(v);
    else if (!strcmp(k, "type")) {
        if (strcmp(v, "story") || !c->title[0] || (c->region && c->region != region_id)) return true;
        for (int i = 0; i < m->nids; i++) if (!strcmp(m->ids[i], c->id)) return true;   /* schon dabei */
        news_card(&m->list[m->n++], c, c->region ? "TAGESSCHAU REGIONAL" : "TAGESSCHAU", NULL);
        if (m->nids < 40) copy(m->ids[m->nids++], sizeof m->ids[0], c->id);
        if (m->n >= m->cap) return false;
    }
    return true;
}

static void fetch_news(void)
{
    status = "tagesschau";
    static home_t h;
    memset(&h, 0, sizeof h);
    json_t j;
    json_init(&j, home_cb, &h);
    h.j = &j;
    source_get("https://www.tagesschau.de/api2u/homepage/", feed_json, &j);
    if (!h.ntop) return;

    static card_t list[16];
    static detail_t d;
    static more_t m;
    memset(&m, 0, sizeof m);
    int n = 0;
    /* die Top-Meldungen zuerst - mit den ersten Absaetzen des Artikels */
    for (int i = 0; i < h.ntop; i++) {
        memset(&d, 0, sizeof d);
        if (h.top[i].details[0]) {
            json_init(&j, detail_cb, &d);
            source_get(h.top[i].details, feed_json, &j);
        }
        news_card(&list[n++], &h.top[i], "TAGESSCHAU", d.text);
        copy(m.ids[m.nids++], sizeof m.ids[0], h.top[i].id);
    }
    /* dann die Region */
    for (int i = 0; i < h.nreg && n < 16; i++) {
        news_card(&list[n++], &h.reg[i], "TAGESSCHAU REGIONAL", NULL);
        copy(m.ids[m.nids++], sizeof m.ids[0], h.reg[i].id);
    }
    /* und mit den uebrigen aktuellen Meldungen auffuellen */
    m.list = list + n;
    m.cap = 16 - n;
    static const char *const MORE[] = { "https://www.tagesschau.de/api2u/news/",
        "https://www.tagesschau.de/api2u/news/?ressort=inland", "https://www.tagesschau.de/api2u/news/?ressort=ausland",
        "https://www.tagesschau.de/api2u/news/?ressort=wirtschaft" };
    for (size_t k = 0; k < sizeof MORE / sizeof *MORE && m.n < m.cap; k++) {
        json_init(&j, more_cb, &m);
        source_get(MORE[k], feed_json, &j);
    }
    n += m.n;
    cards_set(SRC_NEWS, list, n);
    ESP_LOGI(TAG, "Tagesschau: %d top, %d regional, %d weitere", h.ntop, h.nreg, m.n);
}

/* ---------------------------------------------------------------- JW.ORG Nachrichten (RSS) */

typedef struct { card_t *list; int n, cap; } jw_t;

static bool feed_rss(const char *d, size_t n, void *ctx) { return rss_feed((rss_t *)ctx, d, n); }

static bool jw_cb(const char *title, const char *desc, void *ctx)
{
    jw_t *s = ctx;
    card_t *c = &s->list[s->n++];
    card_begin(c, CARD_TEXT, SRC_JW, "JW.ORG NACHRICHTEN");
    card_part(c, title, T_TITLE);
    card_part(c, desc, T_BODY);
    strcpy(c->mood, "!");
    card_finish(c);
    return s->n < s->cap;
}

static void fetch_jw(void)
{
    /* nur waehrend des Abrufs (~7 KB): dauerhaft fehlt das TLS fuer seine 16-KB-Puffer */
    card_t *list = calloc(6, sizeof *list);
    rss_t *r = malloc(sizeof *r);
    if (!list || !r) { free(list); free(r); fetch_failed = true; return; }
    jw_t s = { list, 0, 6 };
    rss_init(r, jw_cb, &s);
    int code = source_get("https://www.jw.org/de/nachrichten/rss/FullNewsRSS/feed.xml", feed_rss, r);
    /* der Rueckruf bricht nach dem letzten Platz ab: das ist kein Fehler */
    if (s.n) { cards_set(SRC_JW, list, s.n); fetch_failed = false; }
    else if (code == 200) fetch_failed = true;
    free(list);
    free(r);
}

/* ---------------------------------------------------------------- Wikipedia: Ereignisse und Geburtstage */

typedef struct { card_t *list; int n, cap, seen; char text[500]; int year, src; } otd_t;

static bool otd_cb(const char *k, const char *v, int depth, void *ctx)
{
    otd_t *s = ctx;
    if (depth != 3) return true;   /* {"events":[ {"text":..,"pages":[..],"year":..} ]} */
    if (!strcmp(k, "text")) copy(s->text, sizeof s->text, v);
    else if (!strcmp(k, "year")) s->year = atoi(v);
    if (!s->text[0] || !s->year) return true;
    /* Zufallsauswahl ueber alle Eintraege (Reservoir): jeder hat dieselbe Chance, dabei zu sein */
    int i = s->seen++;
    int slot = i < s->cap ? i : (int)(esp_random() % (uint32_t)(i + 1));
    if (i < s->cap) s->n++;
    if (slot < s->cap) {
        struct tm lt = today();
        card_t *c = &s->list[slot];
        char head[40], y[24];
        if (s->src == SRC_WIKI_HEUTE) snprintf(head, sizeof head, "HEUTE VOR %d JAHREN", lt.tm_year + 1900 - s->year);
        else snprintf(head, sizeof head, "GEBOREN AM %d.%d.", lt.tm_mday, lt.tm_mon + 1);
        snprintf(y, sizeof y, "%d", s->year);
        card_begin(c, CARD_TEXT, s->src, head);
        card_part(c, y, T_GOLD);
        card_part(c, s->text, s->src == SRC_WIKI_HEUTE ? T_TITLE : T_BODY);
        strcpy(c->mood, s->src == SRC_WIKI_HEUTE ? "AHA!" : "HAPPY!");
        card_finish(c);
    }
    s->text[0] = 0; s->year = 0;
    return true;
}

static void fetch_onthisday(const char *what, int src)
{
    status = "wikipedia heute";
    struct tm lt = today();
    char url[120];
    snprintf(url, sizeof url, "https://api.wikimedia.org/feed/v1/wikipedia/de/onthisday/%s/%02d/%02d", what, lt.tm_mon + 1, lt.tm_mday);
    static card_t list[8];
    static otd_t s;
    memset(&s, 0, sizeof s);
    s.list = list; s.cap = cards_capacity(src); s.src = src;
    json_t j;
    json_init(&j, otd_cb, &s);
    source_get(url, feed_json, &j);
    if (s.n) cards_set(src, list, s.n);
    ESP_LOGI(TAG, "Wikipedia %s: %d", what, s.n);
}

/* ---------------------------------------------------------------- Wikipedia: Zufallsartikel */

typedef struct { char title[120], extract[600], cand[120]; } rnd_t;

/* {"query":{"pages":[{"title":..,"extract":..},..]}} - der laengste Auszug gewinnt */
static bool rnd_cb(const char *k, const char *v, int depth, void *ctx)
{
    rnd_t *s = ctx;
    if (depth != 4) return true;
    if (!strcmp(k, "title")) snprintf(s->cand, sizeof s->cand, "%.119s", v);
    else if (!strcmp(k, "extract") && strlen(v) > strlen(s->extract)) {
        snprintf(s->extract, sizeof s->extract, "%.599s", v);
        memcpy(s->title, s->cand, sizeof s->title);
    }
    return true;
}

static void fetch_random_article(void)
{
    status = "wikipedia zufall";
    static rnd_t s;
    memset(&s, 0, sizeof s);
    json_t j;
    json_init(&j, rnd_cb, &s);
    /* Action-API statt REST: keine Weiterleitung, drei Kandidaten auf einmal */
    if (source_get("https://de.wikipedia.org/w/api.php?action=query&format=json&formatversion=2"
                   "&generator=random&grnnamespace=0&grnlimit=3&prop=extracts&exintro=1"
                   "&explaintext=1&exlimit=3&exchars=580", feed_json, &j) != 200) return;
    if (!s.title[0] || strlen(s.extract) < 80) return;   /* zu duenn: naechstes Mal */
    card_t c;
    card_begin(&c, CARD_TEXT, SRC_WIKI_ZUFALL, "WUSSTEST DU?");
    card_part(&c, s.title, T_BLUE);
    card_part(&c, s.extract, T_BODY);
    strcpy(c.mood, "OHO!");
    card_finish(&c);
    cards_push(SRC_WIKI_ZUFALL, &c);
    ESP_LOGI(TAG, "Wikipedia zufall: %.40s (%u Zeichen)", s.title, (unsigned)strlen(s.extract));
}

/* ---------------------------------------------------------------- Fakten */

static card_t facts[1];
static int fact_module;
static int nfacts;

static void fact(int icon, const char *head, const char *big, const char *l1, const char *l2, const char *mood)
{
    if (nfacts >= 1) return;
    card_t *c = &facts[nfacts++];
    card_begin(c, CARD_FACT, fact_module+1, head);
    c->icon = (uint8_t)icon;
    copy(c->big, sizeof c->big, big);
    if (l1) card_part(c, l1, T_TITLE);
    if (l2) card_part(c, l2, T_BODY);
    copy(c->mood, sizeof c->mood, mood);
    card_finish(c);
}

typedef struct { float v[4]; char s[2][96]; int n, i, best_i; float best; char date[16], name[60]; bool ok, found; } gen_t;

/* Sonne: Auf- und Untergang aus dem Wetter */
static void fact_sun(void)
{
    if (!weather.sunrise_min || !weather.sunset_min) return;
    int len = weather.sunset_min - weather.sunrise_min;
    char big[16], l1[64], l2[64];
    snprintf(big, sizeof big, "%dH%02d", len / 60, len % 60);
    snprintf(l1, sizeof l1, "Sonne von %02d:%02d bis %02d:%02d", weather.sunrise_min / 60, weather.sunrise_min % 60,
             weather.sunset_min / 60, weather.sunset_min % 60);
    snprintf(l2, sizeof l2, "So lange ist es heute hell.");
    fact(ICON_SUN, "SONNE", big, l1, l2, "HELL!");
}

/* Mond: Phase aus dem Datum (Neumond am 6.1.2000 18:14 UTC, synodischer Monat 29,53 Tage) */
static void fact_moon(void)
{
    double age = fmod((time(NULL) - 947182440.0) / 86400.0, 29.530588853);
    int pct = (int)lround((1 - cos(age / 29.530588853 * 2 * M_PI)) / 2 * 100);
    const char *name = age < 1.8 ? "Neumond" : age < 7.4 ? "Zunehmende Sichel" : age < 9.2 ? "Erstes Viertel"
                     : age < 14.0 ? "Zunehmender Mond" : age < 15.8 ? "Vollmond" : age < 22.1 ? "Abnehmender Mond"
                     : age < 23.9 ? "Letztes Viertel" : age < 27.7 ? "Abnehmende Sichel" : "Neumond";
    int to_full = (int)lround(fmod(14.765 - age + 29.53, 29.53));
    char big[12], l2[48];
    snprintf(big, sizeof big, "%d%%", pct);
    if (to_full <= 1) snprintf(l2, sizeof l2, "Heute Nacht ist Vollmond!");
    else snprintf(l2, sizeof l2, "Vollmond in %d Tagen", to_full);
    fact(ICON_MOON, "MOND", big, name, l2, "UHU!");
}

/* Kalender: Kalenderwoche, Tag im Jahr, Wochenende */
static void fact_calendar(void)
{
    struct tm lt = today();
    char kw[8], big[12], l1[40], l2[40];
    strftime(kw, sizeof kw, "%V", &lt);
    snprintf(big, sizeof big, "KW %d", atoi(kw));
    int days_in_year = ((lt.tm_year + 1900) % 4 == 0) ? 366 : 365;
    snprintf(l1, sizeof l1, "Tag %d von %d", lt.tm_yday + 1, days_in_year);
    int to_sat = (6 - lt.tm_wday + 7) % 7;
    if (lt.tm_wday == 0 || lt.tm_wday == 6) snprintf(l2, sizeof l2, "Es ist Wochenende!");
    else snprintf(l2, sizeof l2, "Noch %d Tag%s bis zum Wochenende", to_sat, to_sat == 1 ? "" : "e");
    fact(ICON_CAL, "KALENDER", big, l1, l2, "OK!");
}

static bool ren_cb(const char *k, const char *v, int depth, void *ctx)
{
    gen_t *g = ctx;
    if (!strcmp(k, "data") && strcmp(v, "null")) { g->v[0] = strtof(v, NULL); g->ok = true; }   /* der letzte Wert zaehlt */
    else if (!strcmp(k, "days")) copy(g->date, sizeof g->date, v);
    return true;
}

static bool air_cb(const char *k, const char *v, int depth, void *ctx)
{
    gen_t *g = ctx;
    if (depth != 2) return true;
    if (!strcmp(k, "european_aqi")) { g->v[0] = strtof(v, NULL); g->ok = true; }
    else if (!strcmp(k, "uv_index")) g->v[1] = strtof(v, NULL);
    else if (!strcmp(k, "birch_pollen")) g->v[2] = strtof(v, NULL);
    else if (!strcmp(k, "grass_pollen")) g->v[3] = strtof(v, NULL);
    return true;
}

static bool quake_cb(const char *k, const char *v, int depth, void *ctx)
{
    gen_t *g = ctx;
    /* {"metadata":{"count":17},"features":[{"properties":{"mag":4.8,"place":".."}}]} */
    if (depth == 2 && !strcmp(k, "count")) { g->n = atoi(v); g->ok = true; }
    else if (depth == 4 && !strcmp(k, "mag")) g->v[0] = strtof(v, NULL);
    else if (depth == 4 && !strcmp(k, "place") && g->v[0] > g->best) { g->best = g->v[0]; copy(g->s[0], sizeof g->s[0], v); }
    return true;
}

static bool holiday_cb(const char *k, const char *v, int depth, void *ctx)
{
    gen_t *g = ctx;
    if (g->found) return false;
    /* [{"date":..,"localName":..,"global":true,"counties":["DE-BW",..],"launchYear":..}] */
    if (depth == 2 && !strcmp(k, "date")) { copy(g->date, sizeof g->date, v); g->ok = false; g->name[0] = 0; }
    else if (depth == 2 && !strcmp(k, "localName")) copy(g->name, sizeof g->name, v);
    else if (depth == 2 && !strcmp(k, "global") && !strcmp(v, "true")) g->ok = true;
    else if (depth == 3 && !strcmp(k, "counties") && !strcmp(v, state_code)) g->ok = true;
    else if (depth == 2 && !strcmp(k, "launchYear") && g->ok) g->found = true;   /* Ende des Eintrags */
    return true;
}

static bool num_cb(const char *k, const char *v, int depth, void *ctx)
{
    gen_t *g = ctx;
    if (depth == 1 && !strcmp(k, "number")) { g->v[0] = strtof(v, NULL); g->ok = true; }
    else if (depth == 2 && !strcmp(k, "eur")) g->v[1] = strtof(v, NULL);
    else if (depth == 2 && !strcmp(k, "USD")) g->v[2] = strtof(v, NULL);
    return true;
}

static void fetch_fact(int m) {
 fact_module=m;nfacts=0;gen_t g={0};json_t j;char big[12],l1[80],l2[96];
 if(m==MOD_SUN)fact_sun();
 if(m==MOD_MOON)fact_moon();
 if(m==MOD_DATE)fact_calendar();
    if(m==MOD_ENERGY) {
    memset(&g, 0, sizeof g);
    json_init(&j, ren_cb, &g);
    source_get("https://api.energy-charts.info/ren_share_daily_avg?country=de", feed_json, &j);
    if (g.ok) {
        snprintf(big, sizeof big, "%.0f%%", g.v[0]);
        snprintf(l2, sizeof l2, "Strommix Deutschland, %.6s", g.date);   /* "02.10." */
        fact(ICON_LEAF, "OEKOSTROM", big, "kam aus Wind, Sonne und Co.", l2, g.v[0] > 60 ? "GRUEN!" : "HM.");
    }

    }
    if(m==MOD_AIR) {
    memset(&g, 0, sizeof g);
    json_init(&j, air_cb, &g);
    char url[200];
    snprintf(url, sizeof url, "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.3f&longitude=%.3f"
             "&current=european_aqi,uv_index,birch_pollen,grass_pollen", lat, lon);
    source_get(url, feed_json, &j);
    if (g.ok) {
        int q = (int)g.v[0];
        const char *word = q <= 20 ? "sehr gut" : q <= 40 ? "gut" : q <= 60 ? "maessig" : q <= 80 ? "schlecht" : "sehr schlecht";
        snprintf(big, sizeof big, "%d", q);
        snprintf(l1, sizeof l1, "Luftqualitaet %s", word);
        if (g.v[2] + g.v[3] > 10) snprintf(l2, sizeof l2, "UV %.1f - Pollen unterwegs!", g.v[1]);
        else snprintf(l2, sizeof l2, "UV-Index %.1f", g.v[1]);
        fact(ICON_AIR, "LUFT", big, l1, l2, q <= 40 ? "FRISCH!" : "HUST!");
    }

    }
    if(m==MOD_QUAKE) {
    memset(&g, 0, sizeof g);
    json_init(&j, quake_cb, &g);
    source_get("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson", feed_json, &j);
    if (g.ok && g.n) {
        snprintf(big, sizeof big, "M%.1f", g.best);
        snprintf(l1, sizeof l1, "%d Erdbeben ueber 4,5 in 24 h", g.n);
        snprintf(l2, sizeof l2, "Staerkstes: %.80s", g.s[0]);
        fact(ICON_QUAKE, "ERDBEBEN", big, l1, l2, "WACKEL!");
    }

    }
    if(m==MOD_HOLIDAY) {
    memset(&g, 0, sizeof g);
    json_init(&j, holiday_cb, &g);
    source_get("https://date.nager.at/api/v3/NextPublicHolidays/DE", feed_json, &j);
    if (g.found) {
        struct tm lt = today(), hd = { 0 };
        hd.tm_year = atoi(g.date) - 1900; hd.tm_mon = atoi(g.date + 5) - 1; hd.tm_mday = atoi(g.date + 8);
        struct tm t0 = lt; t0.tm_hour = t0.tm_min = t0.tm_sec = 0;
        int days = (int)lround(difftime(mktime(&hd), mktime(&t0)) / 86400);
        if (days <= 0) { snprintf(big, sizeof big, "HEUTE"); snprintf(l1, sizeof l1, "Heute ist %s!", g.name); }
        else { snprintf(big, sizeof big, "%d", days); snprintf(l1, sizeof l1, "Tage bis %s", g.name); }
        snprintf(l2, sizeof l2, "Am %d.%d.", hd.tm_mday, hd.tm_mon + 1);
        fact(ICON_PARTY, "FEIERTAG", big, l1, l2, "FREI!");
    }

    }
    if(m==MOD_SPACE) {
    memset(&g, 0, sizeof g);
    json_init(&j, num_cb, &g); source_get("http://api.open-notify.org/astros.json", feed_json, &j);
    if (g.ok) {
        snprintf(big, sizeof big, "%.0f", g.v[0]);
        fact(ICON_ROCKET, "WELTALL", big, "Menschen sind gerade", "im Weltall", "WOW!");
    }
    }

 if(m==MOD_BITCOIN) {json_init(&j,num_cb,&g);source_get("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=eur",feed_json,&j);if(g.v[1]>0){snprintf(big,sizeof big,"%.0fK",g.v[1]/1000);snprintf(l1,sizeof l1,"1 Bitcoin = %.0f Euro",g.v[1]);fact(ICON_COIN,"BITCOIN",big,l1,NULL,"KLING!");}}
 if(m==MOD_EXCHANGE) {json_init(&j,num_cb,&g);source_get("https://api.frankfurter.app/latest?from=EUR&to=USD",feed_json,&j);if(g.v[2]>0){snprintf(big,sizeof big,"%.3f",g.v[2]);fact(ICON_COIN,"EURO / DOLLAR",big,"US-Dollar fuer einen Euro",NULL,"OK!");}}
 if(nfacts)cards_set(m+1,facts,nfacts);else fetch_failed=true;
}

/* ---------------------------------------------------------------- Ablauf */

static void clock_card(void){
 struct tm tm=today();char t[20],d[32];strftime(t,sizeof t,"%H:%M",&tm);strftime(d,sizeof d,"%d.%m.%Y",&tm);
 card_t c;card_begin(&c,CARD_FACT,SRC_CLOCK,"UHRZEIT");snprintf(c.big,sizeof c.big,"%.5s",t);card_part(&c,d,T_TITLE);card_finish(&c);cards_set(SRC_CLOCK,&c,1);
}
static void task(void *arg) {
 uint32_t rev=0;int64_t due[MODULE_COUNT]={0};bool location=false;char last_city[64]="";
 for(;;){
 fetching=true;
 if(paused){fetching=false;vTaskDelay(pdMS_TO_TICKS(250));continue;}
 calendar_process_check();char cfg_city[64];uint8_t cfg_region;config_location(cfg_city,sizeof cfg_city,&cfg_region);uint32_t nowrev=config_revision();
 if(rev!=nowrev){memset(due,0,sizeof due);rev=nowrev;if(strcmp(last_city,cfg_city)){location=false;weather.valid=false;snprintf(last_city,sizeof last_city,"%s",cfg_city);}}
 for(int order=0;order<MODULE_COUNT;order++){
 if(paused)break;
 int m=config_order(order);int64_t now=esp_timer_get_time()/1000000;
 if(!config_enabled(m)||now<due[m])continue;
 if(!net_time_valid()){module_end(m,"Warte auf WLAN und Uhrzeit");continue;}
 bool local=m==MOD_MOON||m==MOD_DATE||m==MOD_CLOCK;
 if(!local&&!net_connected()){module_end(m,"WLAN nicht verbunden; letzte Daten bleiben sichtbar");continue;}
 module_begin(m);status=modules[m].id;fetch_failed=false;
 bool needs_location=m==MOD_WEATHER||m==MOD_AIR||m==MOD_SUN;
 if(needs_location&&!location)location=fetch_location();
 if(needs_location&&!location){module_end(m,"Ort fehlt oder wurde nicht gefunden");due[m]=now+120;continue;}
 region_id=cfg_region;for(size_t k=0;k<sizeof STATES/sizeof *STATES;k++)if(STATES[k].region==region_id)snprintf(state_code,sizeof state_code,"%s",STATES[k].code);
 switch(m){
 case MOD_NEWS:fetch_news();break;case MOD_WEATHER:fetch_weather();break;
 case MOD_HISTORY:fetch_onthisday("events",SRC_WIKI_HEUTE);break;case MOD_BIRTHDAYS:fetch_onthisday("births",SRC_WIKI_GEBURT);break;
 case MOD_RANDOM:fetch_random_article();break;case MOD_CALENDAR:calendar_fetch();break;case MOD_CLOCK:clock_card();break;case MOD_JW:fetch_jw();break;
 default:if(m==MOD_SUN&&!weather.valid)fetch_weather();fetch_fact(m);break;
 }
 module_end(m,fetch_failed?"Quelle derzeit nicht erreichbar oder keine gültigen Daten":NULL);
 due[m]=now+(fetch_failed?120:modules[m].interval);if(config_revision()!=rev)break;
 }
 status="wartet";fetching=false;vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
void sources_start(void){cards_init();xTaskCreate(task,"quellen",24576,NULL,1,NULL);}
