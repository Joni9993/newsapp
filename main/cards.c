#include "cards.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

/* Wie viele Karten je Quelle - zusammen etwa 45 KB */
static const uint8_t CAP[SRC_COUNT] = {1, 16, 1, 8, 4, 4, 1, 1,  1,
                                       1, 1,  1, 1, 1, 1, 1, 20, 1, 6};
#define TOTAL 71
static card_t pool[TOTAL];
static card_t *slot[SRC_COUNT];
static uint8_t count[SRC_COUNT], cursor[SRC_COUNT];
static SemaphoreHandle_t mtx;
static int plan_pos;
static card_t history[8];
static int history_count, history_pos = -1;
static bool replay;

void cards_init(void) {
  mtx = xSemaphoreCreateMutex();
  int off = 0;
  for (int s = 0; s < SRC_COUNT; s++) {
    slot[s] = &pool[off];
    off += CAP[s];
  }
}

int cards_capacity(int src) { return CAP[src]; }
int cards_count(int src) { return count[src]; }

void cards_set(int src, const card_t *list, int n) {
  if (n > CAP[src])
    n = CAP[src];
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(slot[src], list, sizeof(card_t) * n);
  count[src] = (uint8_t)n;
  xSemaphoreGive(mtx);
}

void cards_push(int src, const card_t *c) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (count[src] == CAP[src]) {
    memmove(&slot[src][0], &slot[src][1], sizeof(card_t) * (CAP[src] - 1));
    count[src]--;
  }
  slot[src][count[src]++] = *c;
  xSemaphoreGive(mtx);
}

void cards_previous(void) {
  if (history_pos > 0)
    history_pos--;
  replay = true;
}
void cards_replay(void) { replay = true; }
void cards_filter(void) {
  bool enabled[MODULE_COUNT];
  for (int i = 0; i < MODULE_COUNT; i++)
    enabled[i] = config_enabled(i);
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 1; i < SRC_COUNT; i++)
    if (!enabled[i - 1])
      count[i] = 0;
  history_count = 0;
  history_pos = -1;
  replay = false;
  xSemaphoreGive(mtx);
}
bool cards_next(card_t *out) {
  if (replay && history_pos >= 0) {
    *out = history[history_pos];
    replay = false;
    return true;
  }
  replay = false;
  if (history_pos + 1 < history_count) {
    *out = history[++history_pos];
    return true;
  }
  bool ok = false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < MODULE_COUNT; i++) {
    int m = config_order(plan_pos);
    plan_pos = (plan_pos + 1) % MODULE_COUNT;
    int src = m + 1;
    if (!config_enabled(m) || !count[src])
      continue;
    *out = slot[src][cursor[src] % count[src]];
    cursor[src] = (cursor[src] + 1) % count[src];
    ok = true;
    break;
  }
  xSemaphoreGive(mtx);
  if (ok) {
    if (history_count == 8) {
      memmove(history, history + 1, 7 * sizeof(card_t));
      history_count = 7;
    }
    history[history_count++] = *out;
    history_pos = history_count - 1;
  }
  return ok;
}

void card_begin(card_t *c, int kind, int src, const char *head) {
  memset(c, 0, sizeof *c);
  c->kind = (uint8_t)kind;
  c->src = (uint8_t)src;
  text_simplify(c->head, sizeof c->head, head);
}

void card_part(card_t *c, const char *utf8, uint16_t color) {
  if (c->nparts >= CARD_PARTS)
    return;
  int used = c->nparts
                 ? c->part[c->nparts - 1].off + c->part[c->nparts - 1].len + 1
                 : 0;
  if (used >= CARD_TEXT_MAX - 2)
    return;
  char *dst = c->text + used;
  text_simplify(dst, CARD_TEXT_MAX - used, utf8);
  int len = (int)strlen(dst);
  if (!len)
    return;
  c->part[c->nparts].color = color;
  c->part[c->nparts].off = (uint16_t)used;
  c->part[c->nparts].len = (uint16_t)len;
  c->nparts++;
}

void card_finish(card_t *c) {
  /* FNV-1a ueber Kopf und Text - gleicher Inhalt, gleiche Kennung */
  uint32_t h = 2166136261u;
  for (const char *p = c->head; *p; p++)
    h = (h ^ (uint8_t)*p) * 16777619u;
  for (int i = 0; i < c->nparts; i++)
    for (int k = 0; k < c->part[i].len; k++)
      h = (h ^ (uint8_t)c->text[c->part[i].off + k]) * 16777619u;
  for (const char *p = c->big; *p; p++)
    h = (h ^ (uint8_t)*p) * 16777619u;
  c->id = h ? h : 1;
}
