#include "calendar.h"
#include "cards.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "gfx.h"
#include "ical.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static struct {
  ical_occurrence_t items[ICAL_MAX_RESULTS];
  int count;
  char error[100];
  time_t updated;
  bool checking;
  char identity[CAL_URL_MAX];
} cache[CALENDAR_COUNT];
static volatile unsigned check_mask;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
void calendar_request_check(int slot) {
  if (slot >= 0 && slot < CALENDAR_COUNT)
    __atomic_fetch_or(&check_mask, 1u << slot, __ATOMIC_RELAXED);
}
static bool feed(const char *d, size_t n, void *ctx) {
  return ical_feed(ctx, d, n);
}
static void fetch(int i, const calendar_config_t *cfg) {
  portENTER_CRITICAL(&mux);
  cache[i].checking = true;
  portEXIT_CRITICAL(&mux);
  if (strcmp(cache[i].identity, cfg->url)) {
    memset(cache[i].items, 0, sizeof cache[i].items);
    cache[i].count = 0;
    cache[i].updated = 0;
    snprintf(cache[i].identity, sizeof cache[i].identity, "%s", cfg->url);
  }
  char error[100] = "";
  int count = 0;
  ical_occurrence_t *items = calloc(ICAL_MAX_RESULTS, sizeof *items);
  ical_parser_t *p = NULL;
  if (!cfg->url[0])
    snprintf(error, sizeof error, "Kein Kalender hinterlegt");
  else if (!net_time_valid())
    snprintf(error, sizeof error, "Warte auf gültige Uhrzeit");
  else if (!items || (p = ical_create()) == NULL)
    snprintf(error, sizeof error,
             "Nicht genug freier Speicher (%u KiB frei am Stück)",
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) /
                        1024));
  else {
    /* Google liefert die ganze Historie: nur das Fenster um heute behalten */
    ical_set_window(p, time(NULL));
    int code = net_get_ex(cfg->url, feed, p, ICAL_MAX_BYTES, 120);
    if (ical_error(p)[0])
      snprintf(error, sizeof error, "%s", ical_error(p));
    else if (code == -3)
      snprintf(error, sizeof error,
               "Kalender zu groß oder Abruf zu langsam (über 4 MiB / 2 min)");
    else if (code == 404 || code == 403 || code == 401)
      snprintf(error, sizeof error,
               "Link ungültig oder zurückgesetzt (HTTP %d) - neu kopieren",
               code);
    else if (code != 200)
      snprintf(error, sizeof error, "Kalenderabruf fehlgeschlagen (%d)", code);
    else if (!ical_finish(p, time(NULL), items, &count))
      snprintf(error, sizeof error, "%s", ical_error(p));
  }
  if (p) {
    ESP_LOGI("kalender", "Slot %d: %d Termine, %d verworfen, HTTP ok=%d", i,
             count, ical_dropped(p), !error[0]);
    ical_free(p);
  }
  /* A URL removed/replaced during the request must never publish old private
   * data. */
  board_config_t latest;
  config_get(&latest);
  if (strcmp(latest.calendars[i].url, cfg->url)) {
    free(items);
    return;
  }
  if (!error[0]) {
    memcpy(cache[i].items, items, count * sizeof *items);
    cache[i].count = count;
  }
  free(items);
  portENTER_CRITICAL(&mux);
  cache[i].checking = false;
  snprintf(cache[i].error, sizeof cache[i].error, "%s", error);
  if (!error[0])
    cache[i].updated = time(NULL);
  portEXIT_CRITICAL(&mux);
}
static void publish(const board_config_t *cfg) {
  typedef struct {
    const ical_occurrence_t *e;
    int slot;
  } ref_t;
  static ref_t refs[CALENDAR_COUNT * ICAL_MAX_RESULTS];   /* nur der Quellen-Task ruft publish */
  int n = 0;
  for (int i = 0; i < CALENDAR_COUNT; i++)
    if (cfg->calendars[i].url[0] &&
        !strcmp(cache[i].identity, cfg->calendars[i].url))
      for (int j = 0; j < cache[i].count; j++) {
        const ical_occurrence_t *e = &cache[i].items[j];
        if (e->end < time(NULL) && e->start < time(NULL))
          continue;
        int at = n;
        while (at && refs[at - 1].e->start > e->start) {
          refs[at] = refs[at - 1];
          at--;
        }
        refs[at] = (ref_t){e, i};
        n++;
      }
  /* ~14 KB: nur kurz vom Heap, auf dem Stack des Quellen-Tasks lief es ueber */
  card_t *cards = malloc(ICAL_MAX_RESULTS * sizeof *cards);
  if (!cards)
    return;
  /* Nur ein Tag: heute, sonst der naechste Tag mit Terminen. Laufende Termine
   * zaehlen zu heute. Je Karte bis zu CARD_PARTS Termine, ein Termin je Zeile. */
  time_t now = time(NULL);
  struct tm today, tm;
  localtime_r(&now, &today);
  int day = -1, total = 0, count = 0;
  for (int i = 0; i < n; i++) {
    time_t at = refs[i].e->start > now ? (time_t)refs[i].e->start : now;
    localtime_r(&at, &tm);
    int key = (tm.tm_year - today.tm_year) * 400 + tm.tm_yday;
    if (day < 0)
      day = key;
    if (key == day)
      refs[total++] = refs[i];
  }
  char head[40] = "TERMINE";
  if (total) {
    time_t at = refs[0].e->start > now ? (time_t)refs[0].e->start : now;
    localtime_r(&at, &tm);
    static const char *const wd[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
    int ahead = day - today.tm_yday;
    if (!ahead)
      snprintf(head, sizeof head, "HEUTE: %d TERMIN%s", total,
               total == 1 ? "" : "E");
    else if (ahead == 1)
      snprintf(head, sizeof head, "MORGEN: %d TERMIN%s", total,
               total == 1 ? "" : "E");
    else
      snprintf(head, sizeof head, "%s %d.%d.: %d TERMIN%s", wd[tm.tm_wday],
               tm.tm_mday, tm.tm_mon + 1, total, total == 1 ? "" : "E");
  }
  for (int i = 0; i < total && count < ICAL_MAX_RESULTS; i++) {
    card_t *c = &cards[count];
    if (i % CARD_PARTS == 0)
      card_begin(c, CARD_TEXT, SRC_CALENDAR, head);
    const ical_occurrence_t *e = refs[i].e;
    time_t ts = e->start;
    localtime_r(&ts, &tm);
    char line[200], when[16];
    if (e->all_day)
      snprintf(when, sizeof when, "GANZTAG");
    else if (e->start < now)
      snprintf(when, sizeof when, "JETZT");
    else
      strftime(when, sizeof when, "%H:%M", &tm);
    if (e->location[0] && strlen(e->title) + strlen(e->location) < 40)
      snprintf(line, sizeof line, "%s %s (%s)", when, e->title, e->location);
    else
      snprintf(line, sizeof line, "%s %s", when, e->title);
    card_part(c, line, RGB(255, 255, 255));
    if (i % CARD_PARTS == CARD_PARTS - 1 || i == total - 1) {
      card_finish(c);
      count++;
    }
  }
  if (!count) {
    card_begin(cards, CARD_TEXT, SRC_CALENDAR, "MEIN KALENDER");
    card_part(cards, "Keine Termine in den naechsten sieben Tagen.",
              RGB(255, 255, 255));
    card_finish(cards);
    bool success = false;
    for (int i = 0; i < CALENDAR_COUNT; i++)
      if (cfg->calendars[i].url[0] && cache[i].updated && !cache[i].error[0])
        success = true;
    if (success)
      count = 1;
  }
  cards_set(SRC_CALENDAR, cards, count);
  free(cards);
}
void calendar_fetch(void) {
  board_config_t cfg;
  config_get(&cfg);
  for (int i = 0; i < CALENDAR_COUNT; i++)
    if (cfg.calendars[i].url[0])
      fetch(i, &cfg.calendars[i]);
  config_get(&cfg);
  publish(&cfg);
}
void calendar_process_check(void) {
  unsigned bits = __atomic_exchange_n(&check_mask, 0, __ATOMIC_RELAXED);
  if (!bits)
    return;
  board_config_t cfg;
  config_get(&cfg);
  for (int i = 0; i < CALENDAR_COUNT; i++)
    if (bits & (1u << i))
      fetch(i, &cfg.calendars[i]);
  config_get(&cfg);
  publish(&cfg);
}
cJSON *calendar_status(void) {
  cJSON *a = cJSON_CreateArray();
  board_config_t cfg;
  config_get(&cfg);
  for (int i = 0; i < CALENDAR_COUNT; i++) {
    char err[100];
    bool checking;
    time_t updated;
    portENTER_CRITICAL(&mux);
    snprintf(err, sizeof err, "%s", cache[i].error);
    checking = cache[i].checking;
    updated = cache[i].updated;
    portEXIT_CRITICAL(&mux);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "slot", i);
    cJSON_AddStringToObject(o, "error", cfg.calendars[i].url[0] ? err : "");
    cJSON_AddBoolToObject(
        o, "checking",
        checking ||
            (__atomic_load_n(&check_mask, __ATOMIC_RELAXED) & (1u << i)));
    cJSON_AddNumberToObject(o, "updated",
                            cfg.calendars[i].url[0] ? updated : 0);
    cJSON_AddItemToArray(a, o);
  }
  return a;
}
