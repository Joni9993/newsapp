#include "notify.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define QUEUE 6
static notify_t queue[QUEUE];
static int head, count;
static uint32_t recent[4];
static int64_t recent_at[4];
static int recent_pos;
static char token[33];
static bool enabled = true;
static int privacy = NOTIFY_FULL;
static SemaphoreHandle_t lock;

static void save(void) {
  nvs_handle_t h;
  if (nvs_open("newsboard", NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_str(h, "nt_token", token);
  nvs_set_u8(h, "nt_on", enabled);
  nvs_set_u8(h, "nt_priv", (uint8_t)privacy);
  nvs_commit(h);
  nvs_close(h);
}
static void new_token(void) {
  uint8_t b[16];
  esp_fill_random(b, sizeof b);
  for (int i = 0; i < 16; i++)
    snprintf(token + 2 * i, 3, "%02x", b[i]);
}
void notify_init(void) {
  lock = xSemaphoreCreateMutex();
  nvs_handle_t h;
  bool have = false;
  if (nvs_open("newsboard", NVS_READONLY, &h) == ESP_OK) {
    size_t n = sizeof token;
    have = nvs_get_str(h, "nt_token", token, &n) == ESP_OK && strlen(token) == 32;
    uint8_t v;
    if (nvs_get_u8(h, "nt_on", &v) == ESP_OK)
      enabled = v;
    if (nvs_get_u8(h, "nt_priv", &v) == ESP_OK && v <= NOTIFY_APP_ONLY)
      privacy = v;
    nvs_close(h);
  }
  if (!have) {
    new_token();
    save();
  }
}
const char *notify_token(void) { return token; }
bool notify_token_ok(const char *t) {
  if (!t || strlen(t) != 32)
    return false;
  unsigned diff = 0; /* gleich lange Pruefung, verraet nichts ueber die Zeit */
  for (int i = 0; i < 32; i++)
    diff |= (unsigned char)t[i] ^ (unsigned char)token[i];
  return !diff;
}
/* Paketnamen (com.whatsapp) in lesbare Namen */
static void app_name(char *out, size_t n, const char *app) {
  static const char *const known[][2] = {
      {"com.whatsapp", "WhatsApp"},
      {"org.telegram.messenger", "Telegram"},
      {"org.thoughtcrime.securesms", "Signal"},
      {"ch.threema.app", "Threema"},
      {"com.google.android.gm", "Gmail"},
      {"com.microsoft.office.outlook", "Outlook"},
      {"com.google.android.apps.messaging", "SMS"},
      {"com.samsung.android.messaging", "SMS"},
      {"com.google.android.dialer", "Telefon"},
      {"com.samsung.android.dialer", "Telefon"},
      {"com.instagram.android", "Instagram"},
      {"com.facebook.orca", "Messenger"},
      {"com.discord", "Discord"},
      {"com.google.android.calendar", "Kalender"},
  };
  for (size_t i = 0; i < sizeof known / sizeof *known; i++)
    if (!strcmp(app, known[i][0])) {
      snprintf(out, n, "%.31s", known[i][1]);
      return;
    }
  /* Unbekannter Paketname (de.dhl.paket): den aussagekraeftigsten Teil nehmen */
  if (strchr(app, '.') && !strchr(app, ' ')) {
    static const char *const filler[] = {"com", "org", "de", "net", "android",
                                         "app", "apps", "mobile", "google", "client"};
    char best[32] = "";
    const char *p = app;
    while (*p) {
      size_t len = strcspn(p, ".");
      bool skip = len < 2 || len >= sizeof best;
      for (size_t i = 0; !skip && i < sizeof filler / sizeof *filler; i++)
        if (strlen(filler[i]) == len && !strncmp(p, filler[i], len))
          skip = true;
      if (!skip && !best[0]) {
        memcpy(best, p, len);
        best[len] = 0;
      }
      p += len;
      if (*p == '.')
        p++;
    }
    if (best[0]) {
      best[0] = (char)toupper((unsigned char)best[0]);
      snprintf(out, n, "%.31s", best);
      return;
    }
  }
  snprintf(out, n, "%.31s", app[0] ? app : "Nachricht");
}
static bool has(const char *hay, const char *needle) {
  char low[48];
  size_t i = 0;
  for (; hay[i] && i + 1 < sizeof low; i++)
    low[i] = (char)tolower((unsigned char)hay[i]);
  low[i] = 0;
  return strstr(low, needle) != NULL;
}
static int kind_of(const char *app) {
  static const char *const chat[] = {"whatsapp", "telegram", "signal", "threema",
                                     "sms",      "messag",   "discord", "chat",
                                     "instagram"};
  for (size_t i = 0; i < sizeof chat / sizeof *chat; i++)
    if (has(app, chat[i]))
      return NK_CHAT;
  if (has(app, "mail") || has(app, "outlook"))
    return NK_MAIL;
  if (has(app, "telefon") || has(app, "dialer") || has(app, "phone") ||
      has(app, "anruf"))
    return NK_CALL;
  return NK_BELL;
}
static uint32_t fnv(const char *s, uint32_t h) {
  while (*s)
    h = (h ^ (unsigned char)*s++) * 16777619u;
  return h;
}
bool notify_push(const char *app, const char *title, const char *text,
                 const char **why) {
  *why = "";
  if (!enabled) {
    *why = "Push-Nachrichten sind am Board ausgeschaltet";
    return false;
  }
  notify_t n = {0};
  app_name(n.app, sizeof n.app, app ? app : "");
  if (privacy <= NOTIFY_SENDER)
    snprintf(n.title, sizeof n.title, "%.79s", title ? title : "");
  if (privacy == NOTIFY_FULL)
    snprintf(n.text, sizeof n.text, "%.479s", text ? text : "");
  n.at = time(NULL);
  n.kind = kind_of(n.app);
  /* Manche Apps melden dieselbe Nachricht mehrfach (Gruppen, Updates) */
  uint32_t h = fnv(n.text, fnv(n.title, fnv(n.app, 2166136261u)));
  int64_t now = esp_timer_get_time();
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < 4; i++)
    if (recent[i] == h && now - recent_at[i] < 120000000LL) {
      xSemaphoreGive(lock);
      *why = "doppelt";
      return false;
    }
  recent[recent_pos] = h;
  recent_at[recent_pos] = now;
  recent_pos = (recent_pos + 1) % 4;
  if (count == QUEUE) { /* voll: die aelteste faellt raus */
    head = (head + 1) % QUEUE;
    count--;
  }
  queue[(head + count++) % QUEUE] = n;
  xSemaphoreGive(lock);
  return true;
}
bool notify_pop(notify_t *out) {
  if (!lock)
    return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = count > 0;
  if (ok) {
    *out = queue[head];
    head = (head + 1) % QUEUE;
    count--;
  }
  xSemaphoreGive(lock);
  return ok;
}
cJSON *notify_public(void) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "enabled", enabled);
  cJSON_AddNumberToObject(o, "privacy", privacy);
  cJSON_AddStringToObject(o, "token", token);
  return o;
}
bool notify_update(const cJSON *j) {
  const cJSON *on = cJSON_GetObjectItemCaseSensitive(j, "enabled"),
              *pr = cJSON_GetObjectItemCaseSensitive(j, "privacy"),
              *re = cJSON_GetObjectItemCaseSensitive(j, "regenerate");
  if ((on && !cJSON_IsBool(on)) ||
      (pr && (!cJSON_IsNumber(pr) || pr->valueint < 0 ||
              pr->valueint > NOTIFY_APP_ONLY)))
    return false;
  if (on)
    enabled = cJSON_IsTrue(on);
  if (pr)
    privacy = pr->valueint;
  if (cJSON_IsTrue(re))
    new_token();
  save();
  return true;
}
