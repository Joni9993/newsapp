#include "modules.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdio.h>
#include <string.h>
const module_t modules[MODULE_COUNT] = {
    {"news", "Tagesschau", 1200},
    {"weather", "Wetter", 900},
    {"history", "Heute in der Geschichte", 21600},
    {"birthdays", "Geburtstage", 21600},
    {"random", "Wikipedia-Zufall", 900},
    {"sun", "Sonnenzeiten", 3600},
    {"moon", "Mond", 3600},
    {"date", "Kalenderwoche", 3600},
    {"energy", "Ökostrom", 3600},
    {"air", "Luftqualität", 3600},
    {"quake", "Erdbeben", 3600},
    {"holiday", "Feiertage", 21600},
    {"space", "Menschen im Weltall", 3600},
    {"bitcoin", "Bitcoin", 3600},
    {"exchange", "Euro / US-Dollar", 3600},
    {"calendar", "Meine Termine", 900},
    {"clock", "Uhrzeit", 60},
    {"jw", "JW.ORG Nachrichten", 1800}};
static module_status_t states[MODULE_COUNT];
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool module_enabled(int id) {
  return config_enabled(id);
}
void module_begin(int id) {
  portENTER_CRITICAL(&mux);
  states[id].loading = true;
  portEXIT_CRITICAL(&mux);
}
void module_end(int id, const char *error) {
  portENTER_CRITICAL(&mux);
  states[id].loading = false;
  snprintf(states[id].error, sizeof states[id].error, "%s", error ? error : "");
  if (!error)
    states[id].last_success = time(NULL);
  portEXIT_CRITICAL(&mux);
}
void module_status(int id, module_status_t *out) {
  portENTER_CRITICAL(&mux);
  *out = states[id];
  portEXIT_CRITICAL(&mux);
}
int module_from_id(const char *id) {
  for (int i = 0; i < MODULE_COUNT; i++)
    if (!strcmp(modules[i].id, id))
      return i;
  return -1;
}
cJSON *modules_public(void) {
  cJSON *a = cJSON_CreateArray();
  for (int i = 0; i < MODULE_COUNT; i++) {
    module_status_t s;
    module_status(i, &s);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", modules[i].id);
    cJSON_AddStringToObject(o, "name", modules[i].name);
    cJSON_AddNumberToObject(o, "interval", modules[i].interval);
    cJSON_AddNumberToObject(o, "lastSuccess", s.last_success);
    cJSON_AddBoolToObject(o, "loading", s.loading);
    cJSON_AddStringToObject(o, "error", s.error);
    cJSON_AddItemToArray(a, o);
  }
  return a;
}
