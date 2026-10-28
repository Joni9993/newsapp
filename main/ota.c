#include "ota.h"
#include "nvs.h"
#include "config.h"
#include "net.h"
#include "sources.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define TAG "ota"
/* Heap ist knapp (~150 KB): nur ein kleiner Puffer, nie das ganze Image */
#define CHUNK 4096

/* GitHub-Repo fuer das Online-Update (muss oeffentlich sein) */
#define OTA_REPO "Joni9993/newsapp"
#ifdef LCD_LEGACY_PANEL
#define OTA_ASSET "newsboard-legacy.bin"
#else
#define OTA_ASSET "newsboard.bin"
#endif
#ifdef LCD_LEGACY_PANEL
#define RUN_LEGACY true
#else
#define RUN_LEGACY false
#endif
#define MIN_HEAP (60 * 1024)

static volatile bool busy;
static ota_online_t st; /* Zustand, per lock geschuetzt */
static SemaphoreHandle_t lock;
/* Ergebnis der letzten erfolgreichen Pruefung: gibt es ein neueres Release? */
static volatile bool update_avail;
static char update_tag[sizeof st.latest]; /* per lock geschuetzt */
/* Naechste automatische Pruefung in Sekunden seit Start, 0 = noch nicht gesetzt */
static volatile int32_t next_check;
static void lk(void) {
  if (!lock)
    lock = xSemaphoreCreateMutex();
  xSemaphoreTake(lock, portMAX_DELAY);
}
static void ul(void) { xSemaphoreGive(lock); }

/* Quellen anhalten und bis ~20 s warten, bis der laufende Abruf fertig ist:
 * so teilen sich Update und Quellen nicht Heap und Netz. Bei Fehler wieder
 * mit sources_pause(false) freigeben; nach Erfolg startet das Board neu. */
static void sources_hold(void) {
  sources_pause(true);
  for (int i = 0; i < 200 && !sources_idle(); i++)
    vTaskDelay(pdMS_TO_TICKS(100));
}

const char *ota_partition_label(void) {
  const esp_partition_t *p = esp_ota_get_running_partition();
  return p ? p->label : "?";
}

void ota_mark_valid(void) {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (run && esp_ota_get_state_partition(run, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
      ESP_LOGI(TAG, "neue Firmware bestaetigt (%s)", run->label);
    else
      ESP_LOGE(TAG, "Bestaetigung fehlgeschlagen");
  }
}

static esp_err_t reply(httpd_req_t *r, const char *status, bool good,
                       const char *msg) {
  char b[160];
  if (good)
    snprintf(b, sizeof b, "{\"ok\":true}");
  else
    snprintf(b, sizeof b, "{\"error\":\"%s\"}", msg);
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  if (!good) /* ungelesener Body: Verbindung nicht weiter benutzen */
    httpd_resp_set_hdr(r, "Connection", "close");
  return httpd_resp_send(r, b, HTTPD_RESP_USE_STRLEN);
}

/* Neustart erst nach ~1,5 s, damit die Antwort noch beim Browser ankommt */
static void restart_cb(void *arg) { esp_restart(); }
static void restart_soon(void) {
  static esp_timer_handle_t t;
  const esp_timer_create_args_t a = {.callback = restart_cb,
                                     .name = "ota_restart"};
  if (esp_timer_create(&a, &t) != ESP_OK ||
      esp_timer_start_once(t, 1500000) != ESP_OK)
    esp_restart();
}

static int recv_some(httpd_req_t *r, char *buf, size_t n) {
  for (int tries = 0; tries < 5; tries++) {
    int k = httpd_req_recv(r, buf, n);
    if (k == HTTPD_SOCK_ERR_TIMEOUT)
      continue;
    return k;
  }
  return -1;
}

/* Rest des Bodys wegwerfen, damit der Browser die Fehlermeldung noch sieht */
static void drain(httpd_req_t *r, char *buf, size_t left) {
  while (left > 0) {
    int k = recv_some(r, buf, left < CHUNK ? left : CHUNK);
    if (k <= 0)
      return;
    left -= (size_t)k;
  }
}

/* Version endet auf "-legacy" -> Image fuer das alte 2"-Panel */
static bool image_legacy(const esp_app_desc_t *d) {
  size_t n = strnlen(d->version, sizeof d->version);
  return n >= 7 && !memcmp(d->version + n - 7, "-legacy", 7);
}

/* ?force=1: Display-Variante nicht pruefen */
static bool force_requested(httpd_req_t *r) {
  char q[64], v[8];
  return httpd_req_get_url_query_str(r, q, sizeof q) == ESP_OK &&
         httpd_query_key_value(q, "force", v, sizeof v) == ESP_OK &&
         !strcmp(v, "1");
}

esp_err_t ota_upload(httpd_req_t *r) {
  const size_t total = r->content_len;
  const size_t need = sizeof(esp_image_header_t) +
                      sizeof(esp_image_segment_header_t) +
                      sizeof(esp_app_desc_t);
  if (total < need)
    return reply(r, "400 Bad Request", false, "Datei zu klein für eine Firmware");
  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  if (!part)
    return reply(r, "500 Internal Server Error", false,
                 "Keine Update-Partition vorhanden (USB-Erstinstallation nötig)");
  if (total > part->size)
    return reply(r, "413 Payload Too Large", false,
                 "Firmware ist zu groß für die Update-Partition");
  if (busy)
    return reply(r, "409 Conflict", false, "Es läuft bereits ein Update");
  char *buf = malloc(CHUNK);
  if (!buf)
    return reply(r, "503 Service Unavailable", false, "Speicher voll");
  busy = true;

  const char *status = "400 Bad Request", *msg = "Update fehlgeschlagen";
  esp_ota_handle_t h = 0;
  bool begun = false, discard = false;
  size_t got = 0, fill = 0, chunk = 0;
  esp_err_t e;
  esp_app_desc_t desc;
  const esp_app_desc_t *self = esp_app_get_description();

  /* 1. Anfang lesen: Header + App-Beschreibung pruefen, bevor irgendwas
   *    geschrieben wird */
  while (fill < need) {
    int k = recv_some(r, buf + fill, CHUNK - fill);
    if (k <= 0) {
      msg = "Upload abgebrochen oder zu langsam";
      goto fail;
    }
    fill += (size_t)k;
  }
  got = fill;
  memcpy(&desc,
         buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
         sizeof desc);
  discard = true; /* ab hier: Body lesen und verwerfen, dann Fehler melden */
  if ((uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC ||
      desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
    msg = "Das ist keine ESP32-Firmware (.bin)";
    goto fail;
  }
  if (strncmp(desc.project_name, self->project_name,
              sizeof desc.project_name)) {
    msg = "Falsche Firmware: gehört nicht zum Nachrichtenbote";
    goto fail;
  }
  if (image_legacy(&desc) != RUN_LEGACY && !force_requested(r)) {
    msg = image_legacy(&desc)
              ? "Falsche Display-Variante: Datei ist für altes 2\\\"-Panel"
              : "Falsche Display-Variante: Datei ist für Standard-Display";
    goto fail;
  }
  discard = false;
  sources_hold();
  ESP_LOGI(TAG, "Update %s -> %s (%u Bytes) nach %s", self->version,
           desc.version, (unsigned)total, part->label);

  /* 2. Streamen */
  e = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
  if (e != ESP_OK) {
    status = "500 Internal Server Error";
    msg = "Update konnte nicht gestartet werden";
    goto fail;
  }
  begun = true;
  chunk = fill;
  for (;;) {
    e = esp_ota_write(h, buf, chunk);
    if (e != ESP_OK) {
      status = "500 Internal Server Error";
      msg = e == ESP_ERR_OTA_VALIDATE_FAILED ? "Firmware-Image ungültig"
                                              : "Schreiben in den Flash fehlgeschlagen";
      goto fail;
    }
    if (got >= total)
      break;
    int k = recv_some(r, buf, total - got < CHUNK ? total - got : CHUNK);
    if (k <= 0) {
      msg = "Upload abgebrochen oder zu langsam";
      goto fail;
    }
    chunk = (size_t)k;
    got += chunk;
  }

  /* 3. Pruefsumme/Signatur kontrollieren, dann Bootpartition umschalten */
  begun = false;
  e = esp_ota_end(h);
  if (e != ESP_OK) {
    status = "500 Internal Server Error";
    msg = e == ESP_ERR_OTA_VALIDATE_FAILED ? "Firmware-Image ungültig (Prüfsumme)"
                                            : "Update konnte nicht abgeschlossen werden";
    goto fail;
  }
  if (esp_ota_set_boot_partition(part) != ESP_OK) {
    status = "500 Internal Server Error";
    msg = "Bootpartition konnte nicht umgestellt werden";
    goto fail;
  }
  free(buf);
  busy = false;
  ESP_LOGI(TAG, "Update ok, Neustart");
  esp_err_t sent = reply(r, "200 OK", true, NULL);
  restart_soon();
  return sent;

fail:
  ESP_LOGW(TAG, "Update abgelehnt: %s", msg);
  sources_pause(false);
  if (begun)
    esp_ota_abort(h);
  if (discard)
    drain(r, buf, total - got);
  free(buf);
  busy = false;
  return reply(r, status, false, msg);
}

/* ------------------------------------------------------------ Online-Update */
void ota_online_status(ota_online_t *out) {
  lk();
  *out = st;
  ul();
}
static void set_err(const char *m) {
  lk();
  st.state = OTA_ERROR;
  snprintf(st.error, sizeof st.error, "%s", m);
  ul();
  sources_pause(false);
  busy = false;
}
static void run_version(char *out, size_t n) {
  snprintf(out, n, "%s", esp_app_get_description()->version);
  char *l = strstr(out, "-legacy");
  if (l)
    *l = 0;
}

/* Ist tag (z. B. "v1.6") neuer als die laufende Version? Lassen sich beide
 * nicht als Versionsnummer lesen, zaehlt "anders" als neuer. */
static bool newer(const char *tag) {
  char cur[32];
  run_version(cur, sizeof cur);
  int a[3] = {0}, b[3] = {0};
  const char *t = tag + (*tag == 'v'), *c = cur + (cur[0] == 'v');
  int na = sscanf(t, "%d.%d.%d", &a[0], &a[1], &a[2]);
  int nb = sscanf(c, "%d.%d.%d", &b[0], &b[1], &b[2]);
  if (na < 2 || nb < 2)
    return strcmp(tag, cur) != 0;
  for (int i = 0; i < 3; i++)
    if (a[i] != b[i])
      return a[i] > b[i];
  return false;
}

typedef struct {
  char buf[3072];
  size_t n;
  bool found;
  char tag[sizeof st.latest];
} scan_t;
/* Sucht "tag_name":"..." im Anfang der Antwort, der Rest wird nicht gelesen */
static bool scan_cb(const char *d, size_t n, void *ctx) {
  scan_t *s = ctx;
  size_t k = n < sizeof s->buf - 1 - s->n ? n : sizeof s->buf - 1 - s->n;
  memcpy(s->buf + s->n, d, k);
  s->n += k;
  s->buf[s->n] = 0;
  char *p = strstr(s->buf, "\"tag_name\"");
  if (p) {
    p = strchr(p + 10, '"');
    char *e = p ? strchr(p + 1, '"') : NULL;
    if (e && e - p - 1 < (int)sizeof s->tag) {
      memcpy(s->tag, p + 1, e - p - 1);
      s->tag[e - p - 1] = 0;
      s->found = true;
      return false;
    }
  }
  return s->n < sizeof s->buf - 1;
}

/* Eigentliche Pruefung; busy ist schon gesetzt und wird am Ende geloescht */
static void check_run(void) {
  const char *m = NULL;
  scan_t *s = calloc(1, sizeof *s);
  int c = 0;
  if (!s) {
    m = "Speicher voll";
  } else if (esp_get_free_heap_size() < MIN_HEAP) {
    m = "Zu wenig freier Speicher, bitte kurz warten und erneut versuchen";
  } else {
    c = net_get_ex("https://api.github.com/repos/" OTA_REPO "/releases/latest",
                   scan_cb, s, 64 * 1024, 20);
    if (c == -1)
      m = "Keine Verbindung zu GitHub (Internet?)";
    else if (c == -2)
      m = "Uhrzeit noch nicht synchron, bitte später erneut versuchen";
    else if (c == -3 || (c == 200 && !s->found))
      m = "Unerwartete Antwort von GitHub";
    else if (c == 404)
      m = "Kein öffentliches Release gefunden (Repo privat?)";
    else if (c == 403 || c == 429)
      m = "GitHub-Abfragelimit erreicht, später erneut versuchen";
    else if (c != 200)
      m = "GitHub-Abfrage fehlgeschlagen";
  }
  lk();
  if (m) {
    st.state = OTA_ERROR;
    snprintf(st.error, sizeof st.error, "%s", m);
  } else {
    st.state = OTA_IDLE;
    snprintf(st.latest, sizeof st.latest, "%s", s->tag);
    snprintf(update_tag, sizeof update_tag, "%s", s->tag);
    update_avail = newer(s->tag);
  }
  ul();
  /* naechste Pruefung: nach Erfolg in 24 h, nach Fehler in 1 h */
  next_check = (int32_t)(esp_timer_get_time() / 1000000) + (m ? 3600 : 86400);
  free(s);
  busy = false;
}
static void check_task(void *arg) {
  check_run();
  vTaskDelete(NULL);
}
/* Startet die Pruefung im Hintergrund; Ergebnis steht danach in ota_online_status() */
esp_err_t ota_online_check_start(char *err, size_t en) {
  if (busy) {
    snprintf(err, en, "Es läuft bereits ein Update");
    return ESP_ERR_INVALID_STATE;
  }
  busy = true; /* kein Install waehrend der Pruefung */
  lk();
  st.state = OTA_CHECKING;
  st.error[0] = 0;
  ul();
  if (xTaskCreate(check_task, "ota_chk", 8192, NULL, 4, NULL) != pdPASS) {
    set_err("Speicher voll");
    snprintf(err, en, "Speicher voll");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
const char *ota_online_asset(void) { return OTA_ASSET; }
bool ota_online_up_to_date(const char *tag) {
  char cur[32];
  run_version(cur, sizeof cur);
  return !strcmp(cur, tag);
}

/* Location-Header roh mitschneiden: esp_http_client_get_url() liefert die
 * Adresse ohne Query - die signierte Download-Adresse waere dann ungueltig */
typedef struct {
  char *out;
  size_t n;
  bool seen, too_long;
} loc_t;
static esp_err_t loc_event(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value &&
      !strcasecmp(e->header_key, "Location")) {
    loc_t *l = e->user_data;
    l->seen = true;
    if (strlen(e->header_value) >= l->n) {
      l->too_long = true;
      l->out[0] = 0;
    } else if (e->header_value[0] == '/') { /* relativ: gehoert zu github.com */
      snprintf(l->out, l->n, "https://github.com%s", e->header_value);
    } else {
      snprintf(l->out, l->n, "%s", e->header_value);
    }
  }
  return ESP_OK;
}

/* Weiterleitungen selbst aufloesen (GitHub -> anderer Host), liefert Endziel in out.
 * Rueckgabe: 200 ok, sonst HTTP-Status oder -1 (Fehler), -2 (kein https), -3 (kein Speicher) */
static int resolve_url(const char *url, char *out, size_t n) {
  loc_t loc = {.out = out, .n = n};
  esp_http_client_config_t cfg = {.url = url,
                                  .user_agent = "newsboard",
                                  .crt_bundle_attach = esp_crt_bundle_attach,
                                  .disable_auto_redirect = true,
                                  /* GET, nicht HEAD: GitHub signiert die Zieladresse
                                   * fuer die Methode der Anfrage - eine per HEAD
                                   * erhaltene Adresse liefert beim Download 401.
                                   * Der Body der Weiterleitung ist leer. */
                                  .method = HTTP_METHOD_GET,
                                  .buffer_size = 2048,
                                  .buffer_size_tx = 2048,
                                  .timeout_ms = 15000,
                                  .event_handler = loc_event,
                                  .user_data = &loc};
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c)
    return -3;
  int status = -1;
  for (int hop = 0; hop < 4; hop++) {
    loc.seen = loc.too_long = false;
    if (esp_http_client_open(c, 0) != ESP_OK) {
      status = -1;
      break;
    }
    esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    if (status >= 300 && status < 400) {
      if (!loc.seen || loc.too_long || !out[0]) {
        status = -1;
        break;
      }
      if (strncmp(out, "https://", 8)) {
        status = -2;
        break;
      }
      /* Ziel liegt auf einem anderen Host (signierte Download-Adresse): dort
       * nicht nachfragen, die Adresse selbst reicht (der Download holt sie gleich) */
      if (strncmp(out, "https://github.com/", 19)) {
        status = 200;
        break;
      }
      esp_http_client_close(c);
      if (esp_http_client_set_url(c, out) != ESP_OK) {
        status = -1;
        break;
      }
      continue;
    }
    if (status == 200 && hop == 0)
      snprintf(out, n, "%s", url); /* sonst steht die zuletzt abgefragte Adresse schon in out */
    break;
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return status;
}

/* ------------------------------------------- Download in Teilen (Range) */
/* Ein langer TLS-Strom (1,3 MB) scheitert auf dem Board an Heap-Fragmentierung
 * oder Verbindungsabbruechen. Darum: Teile zu 64 KB, jeder mit eigener
 * Verbindung und Range-Anfrage; ein misslungener Teil wird ab dem letzten
 * geschriebenen Byte wiederholt. */
#define PART 65536
#define RETRIES 4
#define HEAD_NEED                                                           \
  (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +        \
   sizeof(esp_app_desc_t))

typedef struct {
  char val[64];
  bool seen;
} cr_t;
static esp_err_t cr_event(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value &&
      !strcasecmp(e->header_key, "Content-Range")) {
    cr_t *c = e->user_data;
    snprintf(c->val, sizeof c->val, "%s", e->header_value);
    c->seen = true;
  }
  return ESP_OK;
}
/* "bytes 0-65535/1359632" */
static bool parse_crange(const char *v, size_t *a, size_t *b, size_t *t) {
  if (strncasecmp(v, "bytes ", 6))
    return false;
  char *e;
  unsigned long x = strtoul(v + 6, &e, 10);
  if (*e != '-')
    return false;
  unsigned long y = strtoul(e + 1, &e, 10);
  if (*e != '/')
    return false;
  unsigned long z = strtoul(e + 1, &e, 10);
  if (!z || y < x || y >= z)
    return false;
  *a = x;
  *b = y;
  *t = z;
  return true;
}

enum { DL_OK, DL_RETRY, DL_AUTH, DL_FATAL };
typedef struct {
  char *url;        /* signierte Adresse, wird bei 401/403 erneuert */
  size_t urlcap;
  const char *orig; /* GitHub-Adresse fuer resolve_url */
  uint8_t *buf;     /* CHUNK Bytes */
  const esp_partition_t *part;
  esp_ota_handle_t h;
  bool begun;
  size_t off, total; /* geschriebene Bytes, Gesamtgroesse (0 = unbekannt) */
  char why[40];      /* kurze Ursache des letzten Fehlversuchs */
  char msg[sizeof st.error]; /* endgueltiger Fehler (DL_FATAL) */
} dl_t;

static void log_heap(const char *what, const dl_t *d) {
  ESP_LOGW(TAG, "%s bei %u/%u: %s, Heap %u, groesster Block %u", what,
           (unsigned)d->off, (unsigned)d->total, d->why,
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
static void set_progress(const dl_t *d) {
  if (!d->total)
    return;
  lk();
  st.progress = (int)((uint64_t)d->off * 100 / d->total);
  ul();
}

/* Anfang pruefen (Magic, Projekt, Display-Variante, Groesse), dann esp_ota_begin */
static int start_ota(dl_t *d) {
  esp_app_desc_t desc;
  memcpy(&desc,
         d->buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
         sizeof desc);
  const esp_app_desc_t *self = esp_app_get_description();
  const char *m = NULL;
  if (d->buf[0] != ESP_IMAGE_HEADER_MAGIC || desc.magic_word != ESP_APP_DESC_MAGIC_WORD)
    m = "Das ist keine ESP32-Firmware (.bin)";
  else if (strncmp(desc.project_name, self->project_name, sizeof desc.project_name))
    m = "Falsche Firmware: gehört nicht zum Nachrichtenbote";
  else if (image_legacy(&desc) != RUN_LEGACY)
    m = "Falsche Display-Variante in der Download-Datei";
  else if (d->total > d->part->size)
    m = "Firmware ist zu groß für die Update-Partition";
  if (m) {
    snprintf(d->msg, sizeof d->msg, "%s", m);
    return DL_FATAL;
  }
  /* Laufendes Image noch PENDING_VERIFY? Dann verweigert esp_ota_begin
   * (ESP_ERR_OTA_ROLLBACK_INVALID_STATE): vorher bestaetigen */
  {
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t ist;
    if (run && esp_ota_get_state_partition(run, &ist) == ESP_OK &&
        ist == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGW(TAG, "laufendes Image noch PENDING_VERIFY, bestaetige vor Update");
      ota_mark_valid();
    }
  }
  esp_err_t e = esp_ota_begin(d->part, OTA_WITH_SEQUENTIAL_WRITES, &d->h);
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "ota_begin: %s, Heap %u", esp_err_to_name(e),
             (unsigned)esp_get_free_heap_size());
    snprintf(d->msg, sizeof d->msg, "Flash-Vorbereitung fehlgeschlagen (%s)",
             esp_err_to_name(e));
    return DL_FATAL;
  }
  d->begun = true;
  return DL_OK;
}

/* Holt einen Teil ab d->off. Schon geschriebene Bytes bleiben (d->off waechst
 * pro Schreibvorgang), ein Wiederholen setzt dort wieder an. */
static int get_chunk(dl_t *d) {
  size_t to = d->off + PART - 1;
  if (d->total && to > d->total - 1)
    to = d->total - 1;
  char range[40];
  snprintf(range, sizeof range, "bytes=%u-%u", (unsigned)d->off, (unsigned)to);
  cr_t cr = {0};
  esp_http_client_config_t cfg = {.url = d->url,
                                  .user_agent = "newsboard",
                                  .crt_bundle_attach = esp_crt_bundle_attach,
                                  .disable_auto_redirect = true,
                                  .method = HTTP_METHOD_GET,
                                  .buffer_size = 4096,
                                  .buffer_size_tx = 2048,
                                  .timeout_ms = 15000,
                                  .keep_alive_enable = false,
                                  .event_handler = cr_event,
                                  .user_data = &cr};
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) {
    snprintf(d->why, sizeof d->why, "Speicher voll");
    return DL_RETRY;
  }
  int rc = DL_RETRY;
  size_t expected = 0, got = 0, fill = 0;
  esp_http_client_set_header(c, "Range", range);
  esp_err_t e = esp_http_client_open(c, 0);
  if (e != ESP_OK) {
    snprintf(d->why, sizeof d->why, "%s", esp_err_to_name(e));
    goto out;
  }
  int64_t clen = esp_http_client_fetch_headers(c);
  int status = esp_http_client_get_status_code(c);
  if (clen < 0) {
    snprintf(d->why, sizeof d->why, "Antwort unlesbar");
    goto out;
  }
  if (status == 401 || status == 403) {
    snprintf(d->why, sizeof d->why, "HTTP %d", status);
    rc = DL_AUTH;
    goto out;
  }
  if (status == 206) {
    size_t a, b, t;
    if (!cr.seen || !parse_crange(cr.val, &a, &b, &t) || a != d->off || b > to) {
      snprintf(d->why, sizeof d->why, "Content-Range ungültig");
      goto out;
    }
    if (d->total && t != d->total) {
      snprintf(d->msg, sizeof d->msg, "Die Download-Datei hat sich während des Downloads geändert");
      rc = DL_FATAL;
      goto out;
    }
    d->total = t;
    expected = b - a + 1;
  } else if (status == 200 && d->off == 0 && clen > 0) {
    /* Server ignoriert Range: ganzer Body in einem Strom */
    d->total = (size_t)clen;
    expected = d->total;
  } else {
    snprintf(d->why, sizeof d->why, "HTTP %d", status);
    goto out;
  }

  while (got < expected) {
    size_t room = CHUNK - fill;
    if (room > expected - got)
      room = expected - got;
    int k = esp_http_client_read(c, (char *)d->buf + fill, (int)room);
    if (k <= 0) {
      snprintf(d->why, sizeof d->why, k < 0 ? "Lesefehler" : "Verbindung beendet");
      goto out;
    }
    got += (size_t)k;
    fill += (size_t)k;
    if (!d->begun) {
      if (fill < HEAD_NEED)
        continue; /* erst den ganzen Anfang sammeln, noch nichts schreiben */
      rc = start_ota(d);
      if (rc != DL_OK)
        goto out;
    }
    e = esp_ota_write(d->h, d->buf, fill);
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "ota_write: %s bei %u", esp_err_to_name(e), (unsigned)d->off);
      snprintf(d->msg, sizeof d->msg, "Schreiben fehlgeschlagen (%s) bei %u KB",
               esp_err_to_name(e), (unsigned)(d->off / 1024));
      rc = DL_FATAL;
      goto out;
    }
    d->off += fill;
    fill = 0;
    set_progress(d);
  }
  if (!d->begun) {
    snprintf(d->msg, sizeof d->msg, "Download-Datei zu klein für eine Firmware");
    rc = DL_FATAL;
    goto out;
  }
  rc = DL_OK;
out:
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return rc;
}

/* true = alles geschrieben; sonst Fehlertext in msg */
static bool dl_run(dl_t *d, char *msg, size_t mn) {
  int fails = 0;
  bool reauth = false, dead = false;
  for (;;) {
    if (d->total && d->off >= d->total)
      return true;
    size_t before = d->off;
    int r = get_chunk(d);
    if (r == DL_OK) {
      fails = 0;
      continue;
    }
    if (r == DL_FATAL) {
      snprintf(msg, mn, "%s", d->msg);
      return false;
    }
    if (r == DL_AUTH && !reauth) {
      reauth = true;
      log_heap("401/403, neue Adresse", d);
      int rs = resolve_url(d->orig, d->url, d->urlcap);
      if (rs == 200)
        continue;
      snprintf(d->why, sizeof d->why, "Adresse erneuern fehlgeschlagen");
      dead = true; /* resolve_url hat die Adresse evtl. ueberschrieben */
    }
    fails = d->off > before ? 1 : fails + 1; /* Fortschritt zaehlt als Neustart */
    log_heap("Teil fehlgeschlagen", d);
    if (dead || fails > RETRIES) {
      if (d->total)
        snprintf(msg, mn, "Download abgebrochen bei %u von %u KB (%s)",
                 (unsigned)(d->off / 1024), (unsigned)(d->total / 1024), d->why);
      else
        snprintf(msg, mn, "Download konnte nicht starten (%s)", d->why);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

static void online_task(void *arg) {
  char url[160], tag[sizeof st.latest], msg[sizeof st.error];
  sources_hold();
  lk();
  snprintf(tag, sizeof tag, "%s", st.latest);
  ul();
  snprintf(url, sizeof url,
           "https://github.com/" OTA_REPO "/releases/download/%s/" OTA_ASSET,
           tag);
  ESP_LOGI(TAG, "Online-Update %s, Heap %u", tag,
           (unsigned)esp_get_free_heap_size());
  dl_t *d = calloc(1, sizeof *d);
  if (d) {
    d->urlcap = 1536;
    d->url = malloc(d->urlcap);
    d->buf = malloc(CHUNK);
  }
  if (!d || !d->url || !d->buf) {
    if (d) {
      free(d->url);
      free(d->buf);
    }
    free(d);
    set_err("Speicher voll");
    vTaskDelete(NULL);
    return;
  }
  d->orig = url;
  int rs = resolve_url(url, d->url, d->urlcap);
  if (rs != 200) {
    ESP_LOGW(TAG, "resolve: %d, Heap %u", rs, (unsigned)esp_get_free_heap_size());
    if (rs > 0)
      snprintf(msg, sizeof msg, "GitHub-Weiterleitung fehlgeschlagen (HTTP %d)", rs);
    else
      snprintf(msg, sizeof msg, "GitHub-Weiterleitung fehlgeschlagen (%s)",
               rs == -2 ? "kein https" : rs == -3 ? "Speicher voll" : "Verbindung");
    goto fail;
  }
  d->part = esp_ota_get_next_update_partition(NULL);
  if (!d->part) {
    snprintf(msg, sizeof msg, "Keine Update-Partition vorhanden (USB-Erstinstallation nötig)");
    goto fail;
  }
  lk();
  st.state = OTA_DOWNLOADING;
  st.progress = 0;
  ul();
  if (!dl_run(d, msg, sizeof msg))
    goto fail;
  if (!d->begun) { /* kann nicht sein, sicherheitshalber */
    snprintf(msg, sizeof msg, "Download abgebrochen oder unvollständig");
    goto fail;
  }
  d->begun = false; /* esp_ota_end gibt das Handle in jedem Fall frei */
  esp_err_t e = esp_ota_end(d->h);
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "ota_end: %s", esp_err_to_name(e));
    snprintf(msg, sizeof msg, "%s",
             e == ESP_ERR_OTA_VALIDATE_FAILED ? "Firmware-Image ungültig (Prüfsumme)"
                                              : "Update konnte nicht abgeschlossen werden");
    goto fail;
  }
  e = esp_ota_set_boot_partition(d->part);
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "set_boot: %s", esp_err_to_name(e));
    snprintf(msg, sizeof msg, "Bootpartition konnte nicht umgestellt werden");
    goto fail;
  }
  free(d->url);
  free(d->buf);
  free(d);
  lk();
  st.state = OTA_DONE;
  st.progress = 100;
  ul();
  ESP_LOGI(TAG, "Online-Update ok, Neustart");
  restart_soon(); /* busy bleibt true */
  vTaskDelete(NULL);
  return;

fail:
  ESP_LOGW(TAG, "Online-Update fehlgeschlagen: %s", msg);
  if (d->begun)
    esp_ota_abort(d->h);
  free(d->url);
  free(d->buf);
  free(d);
  set_err(msg);
  vTaskDelete(NULL);
}

esp_err_t ota_online_start(const char *tag, char *err, size_t en) {
  if (busy) {
    snprintf(err, en, "Es läuft bereits ein Update");
    return ESP_ERR_INVALID_STATE;
  }
  if (!net_connected() || !net_time_valid()) {
    snprintf(err, en, "Keine Internetverbindung oder Uhrzeit nicht synchron");
    return ESP_FAIL;
  }
  if (esp_get_free_heap_size() < MIN_HEAP) {
    snprintf(err, en, "Zu wenig freier Speicher, bitte kurz warten und erneut versuchen");
    return ESP_ERR_NO_MEM;
  }
  lk();
  bool ok = tag && tag[0] && strlen(tag) < sizeof st.latest;
  if (ok)
    for (const char *c = tag; *c; c++)
      if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') ||
            (*c >= 'A' && *c <= 'Z') || *c == '.' || *c == '-' || *c == '_'))
        ok = false;
  if (!ok && st.latest[0]) {
    tag = st.latest;
    ok = true;
  }
  if (!ok) {
    ul();
    snprintf(err, en, "Erst nach Updates suchen");
    return ESP_ERR_INVALID_ARG;
  }
  if (tag != st.latest)
    snprintf(st.latest, sizeof st.latest, "%s", tag);
  st.state = OTA_DOWNLOADING;
  st.progress = 0;
  st.error[0] = 0;
  ul();
  busy = true;
  if (xTaskCreate(online_task, "ota_dl", 8192, NULL, 5, NULL) != pdPASS) {
    set_err("Speicher voll");
    snprintf(err, en, "Speicher voll");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

/* ------------------------------------------------- Automatische Pruefung */
bool ota_update_available(char *tag, size_t n) {
  bool a = update_avail;
  if (a && tag && n) {
    lk();
    snprintf(tag, n, "%s", update_tag);
    ul();
  }
  return a;
}

/* Einmal pro Minute: Modus 1 und 2 pruefen taeglich (erste Pruefung ~3 min
 * nachdem Uhrzeit und Netz da sind). Modus 2 prueft zusaetzlich einmal pro
 * Nacht im Fenster 03:00-04:59 und installiert dann ein neueres Release;
 * scheitert das (z. B. Speicher knapp), gibt es erst in der naechsten Nacht
 * einen neuen Versuch. */
/* Nacht-Update nur fuer echte Releases ("v1.5", nicht "v1.5-3-gabc" oder "dev")
 * und nie zweimal fuer denselben Tag: nach einem Rollback liefe es sonst jede
 * Nacht erneut. Der zuletzt versuchte Tag liegt im NVS. */
static bool auto_install_allowed(const char *tag) {
  const char *v = esp_app_get_description()->version;
  if (v[0] != 'v' || strstr(v, "-g") || strstr(v, "dirty"))
    return false;
  char last[32] = "";
  size_t n = sizeof last;
  nvs_handle_t h;
  if (nvs_open("ota", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_str(h, "auto_tag", last, &n);
    nvs_close(h);
  }
  return strcmp(last, tag) != 0;
}
static void auto_install_remember(const char *tag) {
  nvs_handle_t h;
  if (nvs_open("ota", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_str(h, "auto_tag", tag);
    nvs_commit(h);
    nvs_close(h);
  }
}

static void auto_task(void *arg) {
  int night_checked = -1, night_tried = -1;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    int mode = config_auto_update();
    if (!mode || busy || !net_connected() || !net_time_valid())
      continue;
    int32_t up = (int32_t)(esp_timer_get_time() / 1000000);
    if (!next_check) {
      next_check = up + 180;
      continue;
    }
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    int day = lt.tm_year * 400 + lt.tm_yday;
    bool night = mode == 2 && lt.tm_hour >= 3 && lt.tm_hour < 5;
    char err[96], tag[sizeof update_tag];
    if (night && night_checked != day) {
      night_checked = day;
      ota_online_check_start(err, sizeof err);
    } else if (night && night_tried != day && update_avail) {
      ota_update_available(tag, sizeof tag);
      if (!auto_install_allowed(tag)) {
        night_tried = day;
        continue;
      }
      esp_err_t e = ota_online_start(tag, err, sizeof err);
      if (e == ESP_OK)
        auto_install_remember(tag);
      ESP_LOGI(TAG, "Nacht-Update %s: %s", tag, e == ESP_OK ? "gestartet" : err);
      if (e != ESP_ERR_INVALID_STATE) /* nur "beschaeftigt" wird gleich erneut versucht */
        night_tried = day;
    } else if (up >= next_check) {
      next_check = up + 3600; /* check_run setzt den echten Wert */
      ota_online_check_start(err, sizeof err);
    }
  }
}
void ota_auto_start(void) {
  xTaskCreate(auto_task, "ota_auto", 4096, NULL, 1, NULL);
}
