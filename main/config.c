#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "modules.h"
#include "nvs.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static board_config_t current;
static SemaphoreHandle_t lock;
static uint32_t revision = 1;
void config_defaults(board_config_t *c) {
  memset(c, 0, sizeof *c);
  c->version = 2;
  c->duration = 20;
  c->brightness = 80;
  c->night_brightness = 12;
  c->night_start = 22;
  c->night_end = 6;
  c->auto_update = 2; /* Modus 1 (pruefen + Hinweis), siehe config.h */
  c->region = 1;
  for (int i = 0; i < MODULE_COUNT; i++) {
    c->order[i] = i;
    c->enabled[i] = (i == MOD_NEWS || i == MOD_WEATHER || i == MOD_CLOCK);
  }
  for (int i = 0; i < CALENDAR_COUNT; i++)
    strcpy(c->calendars[i].provider, "google");
}
static bool valid(const board_config_t *c) {
  if (c->version != 2 || c->region < 1 || c->region > 16 || c->mode > 1 ||
      c->theme > 3 || c->brightness > 100 || c->night_brightness > 100 ||
      c->night_start > 23 || c->night_end > 23 || c->auto_update > 3 || c->duration < 5 ||
      c->duration > 120)
    return false;
  unsigned mask = 0;
  for (int i = 0; i < MODULE_COUNT; i++) {
    if (c->order[i] >= MODULE_COUNT || (mask & (1u << c->order[i])))
      return false;
    mask |= 1u << c->order[i];
  }
  if (!memchr(c->ssid, 0, sizeof c->ssid) ||
      !memchr(c->password, 0, sizeof c->password) ||
      !memchr(c->city, 0, sizeof c->city))
    return false;
  for (int i = 0; i < CALENDAR_COUNT; i++) {
    const calendar_config_t *k = &c->calendars[i];
    if (!memchr(k->url, 0, sizeof k->url) ||
        !memchr(k->name, 0, sizeof k->name) ||
        !memchr(k->provider, 0, sizeof k->provider) ||
        !config_url_valid(k->url))
      return false;
  }
  return true;
}
/* Speicherformat vor dem neuen Modul: gleiche Felder, aber 17 Module */
#define OLD_MODULES 17
#define LEGACY(name, cals)                                                     \
  typedef struct {                                                             \
    uint32_t version;                                                          \
    char ssid[33], password[65], city[64];                                     \
    uint8_t region, mode, theme, brightness, night_brightness, night_start,    \
        night_end;                                                             \
    uint16_t duration;                                                         \
    bool enabled[OLD_MODULES];                                                 \
    uint8_t order[OLD_MODULES];                                                \
    calendar_config_t calendars[cals];                                         \
  } name
LEGACY(legacy3_t, 3);
LEGACY(legacy6_t, 6);
static void upgrade(board_config_t *c, const legacy3_t *o,
                    const calendar_config_t *cals, int ncal) {
  config_defaults(c);
  memcpy(c->ssid, o->ssid, sizeof c->ssid);
  memcpy(c->password, o->password, sizeof c->password);
  memcpy(c->city, o->city, sizeof c->city);
  c->region = o->region;
  c->mode = o->mode;
  c->theme = o->theme;
  c->brightness = o->brightness;
  c->night_brightness = o->night_brightness;
  c->night_start = o->night_start;
  c->night_end = o->night_end;
  c->duration = o->duration;
  memcpy(c->enabled, o->enabled, OLD_MODULES);
  memcpy(c->order, o->order, OLD_MODULES);
  c->enabled[OLD_MODULES] = false; /* neue Module hinten, aus */
  c->order[OLD_MODULES] = OLD_MODULES;
  memcpy(c->calendars, cals, ncal * sizeof *cals);
}
bool config_init(void) {
  lock = xSemaphoreCreateMutex();
  config_defaults(&current);
  nvs_handle_t h;
  if (nvs_open("newsboard", NVS_READONLY, &h) != ESP_OK)
    return true;
  /* Aeltere Firmware speicherte weniger Module (17) und Kalender (3 bzw. 6) - uebernehmen */
  board_config_t saved;
  memset(&saved, 0, sizeof saved);
  union {
    board_config_t now;
    legacy3_t l3;
    legacy6_t l6;
  } *buf = calloc(1, sizeof *buf);
  size_t n = sizeof *buf;
  bool ok = buf && nvs_get_blob(h, "config", buf, &n) == ESP_OK;
  /* l6 und now sind gleich gross (Auffuellbytes): die Version unterscheidet sie (alt 1, neu 2) */
  if (ok && n == sizeof buf->now && buf->now.version == 2)
    saved = buf->now;
  else if (ok && n == sizeof buf->l6 && buf->l6.version == 1)
    upgrade(&saved, (const legacy3_t *)&buf->l6, buf->l6.calendars, 6);
  else if (ok && n == sizeof buf->l3 && buf->l3.version == 1)
    upgrade(&saved, &buf->l3, buf->l3.calendars, 3);
  else
    ok = false;
  free(buf);
  if (saved.auto_update > 3) /* altes Auffuellbyte war nie garantiert 0 */
    saved.auto_update = 0;
  ok = ok && valid(&saved);
  if (ok)
    current = saved;
  nvs_close(h);
  return ok;
}
void config_get(board_config_t *out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  *out = current;
  xSemaphoreGive(lock);
}
bool config_enabled(int module) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool on = module >= 0 && module < MODULE_COUNT && current.enabled[module];
  xSemaphoreGive(lock);
  return on;
}
int config_order(int pos) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int m = current.order[pos];
  xSemaphoreGive(lock);
  return m;
}
int config_auto_update(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int m = current.auto_update ? current.auto_update - 1 : 1;
  xSemaphoreGive(lock);
  return m;
}
int config_tty_header(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int m = (current.theme & THEME_TTY_CITY) != 0;
  xSemaphoreGive(lock);
  return m;
}
uint16_t config_duration(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  uint16_t d = current.duration;
  xSemaphoreGive(lock);
  return d;
}
void config_calendar(int slot, calendar_config_t *out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  *out = current.calendars[slot];
  xSemaphoreGive(lock);
}
void config_location(char *city, size_t size, uint8_t *region) {
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(city, size, "%s", current.city);
  *region = current.region;
  xSemaphoreGive(lock);
}
void config_credentials(char ssid[33], char password[65]) {
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(ssid, current.ssid, sizeof current.ssid);
  memcpy(password, current.password, sizeof current.password);
  xSemaphoreGive(lock);
}
bool config_has_ssid(void) {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool set = current.ssid[0] != 0;
  xSemaphoreGive(lock);
  return set;
}
uint32_t config_revision(void) {
  return __atomic_load_n(&revision, __ATOMIC_RELAXED);
}
static bool save_locked(board_config_t *c) {
  nvs_handle_t h;
  if (nvs_open("newsboard", NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t e = nvs_set_blob(h, "config", c, sizeof *c);
  if (e == ESP_OK)
    e = nvs_commit(h);
  nvs_close(h);
  if (e != ESP_OK)
    return false;
  current = *c;
  __atomic_add_fetch(&revision, 1, __ATOMIC_RELAXED);
  return true;
}
bool config_wifi(const char *ssid, const char *password) {
  if (!ssid[0] || strlen(ssid) > 32 || strlen(password) > 64 ||
      (password[0] && strlen(password) < 8))
    return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  /* statisch statt auf dem Stack (~3 KB), geschuetzt durch lock */
  static board_config_t c;
  c = current;
  snprintf(c.ssid, sizeof c.ssid, "%s", ssid);
  snprintf(c.password, sizeof c.password, "%s", password);
  bool ok = save_locked(&c);
  xSemaphoreGive(lock);
  return ok;
}
const char *config_url_problem(const char *u) {
  if (!*u)
    return NULL;
  if (strlen(u) >= CAL_URL_MAX)
    return "Link ist zu lang";
  if (strncmp(u, "https://", 8))
    return strchr(u, '@') && !strstr(u, "://")
               ? "Das ist die Kalender-ID, nicht der Link. Bitte die "
                 "Privatadresse im iCal-Format (endet auf .ics) kopieren"
               : "Link muss mit https:// beginnen";
  const char *host = u + 8, *end = strchr(host, '/');
  if (!end || end == host)
    return "Link hat keinen Pfad (z. B. .../basic.ics)";
  for (const char *p = u; *p; p++)
    if ((unsigned char)*p <= 32 || *p == '\\' || *p == '#')
      return "Link enthält Leerzeichen oder ungültige Zeichen";
  for (const char *p = host; p < end; p++)
    if (*p == '@' || *p == ':' || *p == '[' || *p == ']')
      return "Link darf vor dem ersten / keinen Benutzernamen oder Port "
             "enthalten";
  return NULL;
}
bool config_url_valid(const char *u) { return !config_url_problem(u); }
static bool number(const cJSON *j, const char *key, int min, int max,
                   int *out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
  if (!v)
    return true;
  if (!cJSON_IsNumber(v) || v->valuedouble != v->valueint ||
      v->valueint < min || v->valueint > max)
    return false;
  *out = v->valueint;
  return true;
}
bool config_update(const cJSON *j, char *error, size_t size) {
  bool ok = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  board_config_t c = current;
  snprintf(error, size, "Ungültige Einstellung");
  if (!cJSON_IsObject(j))
    goto done;
#define FIELD(key, min, max)                                                   \
  {                                                                            \
    int n = c.key;                                                             \
    if (!number(j, #key, min, max, &n))                                        \
      goto done;                                                               \
    c.key = n;                                                                 \
  }
  FIELD(region, 1, 16);
  FIELD(mode, 0, 1);
  {
    int n = THEME_FIGURE(c.theme);
    if (!number(j, "theme", 0, 1, &n))
      goto done;
    c.theme = (c.theme & THEME_TTY_CITY) | n;
  }
  FIELD(brightness, 0, 100);
  FIELD(night_brightness, 0, 100);
  FIELD(night_start, 0, 23);
  FIELD(night_end, 0, 23);
  FIELD(duration, 5, 120);
  {
    int n = c.auto_update ? c.auto_update - 1 : 1;
    if (!number(j, "autoUpdate", 0, 2, &n))
      goto done;
    c.auto_update = n + 1;
  }
  {
    int n = (c.theme & THEME_TTY_CITY) != 0;
    if (!number(j, "ttyHeader", 0, 1, &n))
      goto done;
    c.theme = THEME_FIGURE(c.theme) | (n ? THEME_TTY_CITY : 0);
  }
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "city");
  if (v) {
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= sizeof c.city)
      goto done;
    strcpy(c.city, v->valuestring);
  }
  v = cJSON_GetObjectItemCaseSensitive(j, "modules");
  if (v) {
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) != MODULE_COUNT)
      goto done;
    unsigned mask = 0;
    for (int i = 0; i < MODULE_COUNT; i++) {
      const cJSON *o = cJSON_GetArrayItem(v, i),
                  *id = cJSON_GetObjectItemCaseSensitive(o, "id"),
                  *en = cJSON_GetObjectItemCaseSensitive(o, "enabled");
      if (!cJSON_IsString(id) || !cJSON_IsBool(en))
        goto done;
      int m = module_from_id(id->valuestring);
      if (m < 0 || (mask & (1u << m)))
        goto done;
      mask |= 1u << m;
      c.order[i] = m;
      c.enabled[m] = cJSON_IsTrue(en);
    }
  }
  v = cJSON_GetObjectItemCaseSensitive(j, "calendars");
  if (v) {
    int n = cJSON_IsArray(v) ? cJSON_GetArraySize(v) : 0;
    if (n < 1 || n > CALENDAR_COUNT)
      goto done;
    for (int i = 0; i < n; i++) {   /* die Webseite schickt nur die sichtbaren Plaetze */
      calendar_config_t *k = &c.calendars[i];
      const cJSON *o = cJSON_GetArrayItem(v, i);
      const cJSON *name = cJSON_GetObjectItemCaseSensitive(o, "name"),
                  *p = cJSON_GetObjectItemCaseSensitive(o, "provider"),
                  *u = cJSON_GetObjectItemCaseSensitive(o, "url"),
                  *del = cJSON_GetObjectItemCaseSensitive(o, "remove");
      if (!cJSON_IsString(name) ||
          strlen(name->valuestring) >= sizeof k->name || !cJSON_IsString(p) ||
          (strcmp(p->valuestring, "google") && strcmp(p->valuestring, "ics")))
        goto done;
      strcpy(k->name, name->valuestring);
      strcpy(k->provider, p->valuestring);
      if (cJSON_IsTrue(del))
        k->url[0] = 0;
      else if (u) {
        const char *why = cJSON_IsString(u) ? config_url_problem(u->valuestring)
                                            : "Link fehlt";
        if (why) {
          snprintf(error, size, "Kalender %d: %s", i + 1, why);
          goto done;
        }
        strcpy(k->url, u->valuestring);
      }
    }
  }
  if (!valid(&c))
    goto done;
  ok = save_locked(&c);
  if (!ok)
    snprintf(error, size, "Einstellungen konnten nicht gespeichert werden");
done:
  xSemaphoreGive(lock);
  return ok;
}
cJSON *config_public(void) {
  board_config_t c;
  config_get(&c);
  cJSON *j = cJSON_CreateObject();
#define ADD(key) cJSON_AddNumberToObject(j, #key, c.key)
  ADD(region);
  ADD(mode);
  cJSON_AddNumberToObject(j, "theme", THEME_FIGURE(c.theme));
  ADD(brightness);
  ADD(night_brightness);
  ADD(night_start);
  ADD(night_end);
  ADD(duration);
  cJSON_AddNumberToObject(j, "autoUpdate",
                          c.auto_update ? c.auto_update - 1 : 1);
  cJSON_AddNumberToObject(j, "ttyHeader", (c.theme & THEME_TTY_CITY) != 0);
  cJSON_AddStringToObject(j, "city", c.city);
  cJSON_AddStringToObject(j, "ssid", c.ssid);
  cJSON *a = cJSON_AddArrayToObject(j, "modules");
  for (int i = 0; i < MODULE_COUNT; i++) {
    int m = c.order[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", modules[m].id);
    cJSON_AddBoolToObject(o, "enabled", c.enabled[m]);
    cJSON_AddItemToArray(a, o);
  }
  a = cJSON_AddArrayToObject(j, "calendars");
  for (int i = 0; i < CALENDAR_COUNT; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", c.calendars[i].name);
    cJSON_AddStringToObject(o, "provider", c.calendars[i].provider);
    cJSON_AddBoolToObject(o, "configured", c.calendars[i].url[0] != 0);
    cJSON_AddItemToArray(a, o);
  }
  return j;
}
