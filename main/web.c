#include "calendar.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "modules.h"
#include "net.h"
#include "notify.h"
#include "automate_flow.h"
#include "nvs.h"
#include "ota.h"
#include "esp_app_desc.h"
#include "screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
extern const unsigned char js_start[] asm("_binary_app_js_start");
extern const unsigned char js_end[] asm("_binary_app_js_end");
extern const unsigned char css_start[] asm("_binary_style_css_start");
extern const unsigned char css_end[] asm("_binary_style_css_end");
/* Angemeldete Browser; ueberstehen Neustarts (NVS). Die drei Plaetze werden
 * reihum neu vergeben, das Cookie gilt 30 Tage. */
static struct {
  char token[33];
} sessions[3];
static int32_t next_session;
static int attempts;
static int64_t locked_until;
static void headers(httpd_req_t *r) {
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(r, "Content-Security-Policy",
                     "default-src 'self'; script-src 'self'; style-src 'self'; "
                     "connect-src 'self'; img-src 'self'; frame-ancestors "
                     "'none'; base-uri 'none'; form-action 'self'");
}
static esp_err_t json(httpd_req_t *r, cJSON *o) {
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (!s)
    return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Speicher voll");
  headers(r);
  httpd_resp_set_type(r, "application/json; charset=utf-8");
  esp_err_t e = httpd_resp_send(r, s, HTTPD_RESP_USE_STRLEN);
  free(s);
  return e;
}
static esp_err_t error(httpd_req_t *r, const char *status,
                       const char *message) {
  httpd_resp_set_status(r, status);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "error", message);
  return json(r, o);
}
static esp_err_t ok(httpd_req_t *r) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", true);
  return json(r, o);
}
static bool trusted_host(httpd_req_t *r, char host[80]) {
  if (httpd_req_get_hdr_value_str(r, "Host", host, 80) != ESP_OK)
    return false;
  char ip[32];
  net_address(ip, sizeof ip);
  char *port = strchr(host, ':');
  if (port && strcmp(port, ":80"))
    return false;
  size_t len = port ? (size_t)(port - host) : strlen(host);
  return (strlen(ip) == len && !strncmp(host, ip, len)) ||
         (net_setup_active() && len == 11 &&
          !strncmp(host, "192.168.4.1", len));
}
static bool same_origin(httpd_req_t *r) {
  char host[80], origin[100], expected[100], mark[8];
  if (!trusted_host(r, host))
    return false;
  if (httpd_req_get_hdr_value_str(r, "Origin", origin, sizeof origin) != ESP_OK)
    return false;
  snprintf(expected, sizeof expected, "http://%s", host);
  if (strcmp(origin, expected))
    return false;
  return httpd_req_get_hdr_value_str(r, "X-Newsboard", mark, sizeof mark) ==
             ESP_OK &&
         !strcmp(mark, "1");
}
static bool authorized(httpd_req_t *r) {
  char cookie[1024], host[80];
  if (!trusted_host(r, host) ||
      httpd_req_get_hdr_value_str(r, "Cookie", cookie, sizeof cookie) != ESP_OK)
    return false;
  char *save = NULL;
  for (char *p = strtok_r(cookie, ";", &save); p;
       p = strtok_r(NULL, ";", &save)) {
    while (*p == ' ')
      p++;
    if (strncmp(p, "nb_session=", 11))
      continue;
    p += 11;
    for (int i = 0; i < 3; i++)
      if (sessions[i].token[0] && !strcmp(p, sessions[i].token))
        return true;
  }
  return false;
}
static cJSON *body(httpd_req_t *r) {
  if (r->content_len <= 0 || r->content_len > 8192)
    return NULL;
  char *b = calloc(1, r->content_len + 1);
  if (!b)
    return NULL;
  int n = 0;
  while (n < r->content_len) {
    int k = httpd_req_recv(r, b + n, r->content_len - n);
    if (k <= 0) {
      free(b);
      return NULL;
    }
    n += k;
  }
  const char *end = NULL;
  cJSON *o = cJSON_ParseWithLengthOpts(b, n + 1, &end, true);
  free(b);
  return o;
}
/* ------------------------------------------------ Push-Nachrichten vom Handy
 * Kommt nicht aus dem Browser, sondern aus einer App wie MacroDroid: darum kein
 * Cookie, sondern ein eigener Token. Felder app/title/text/token duerfen als
 * Query-Parameter, Formular oder JSON kommen - was die App eben kann. */
static bool is_notify(const char *u) {
  return !strncmp(u, "/api/v1/notify", 14) && (!u[14] || u[14] == '?');
}
static int hex(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}
static void urldecode(char *s) {
  char *o = s;
  for (; *s; s++) {
    if (*s == '+')
      *o++ = ' ';
    else if (*s == '%' && hex(s[1]) >= 0 && hex(s[2]) >= 0) {
      *o++ = (char)(hex(s[1]) * 16 + hex(s[2]));
      s += 2;
    } else
      *o++ = *s;
  }
  *o = 0;
}
static void param(const char *q, const char *key, char *out, size_t n) {
  out[0] = 0;
  if (!q || !q[0])
    return;
  esp_err_t e = httpd_query_key_value(q, key, out, n);
  if (e != ESP_OK && e != ESP_ERR_HTTPD_RESULT_TRUNC)
    out[0] = 0;
  else
    urldecode(out);
}
static void field(const cJSON *j, const char *q, const char *f,
                  const char *key, char *out, size_t n) {
  const cJSON *v = j ? cJSON_GetObjectItemCaseSensitive(j, key) : NULL;
  if (cJSON_IsString(v)) {
    strlcpy(out, v->valuestring, n);
    return;
  }
  param(q, key, out, n);
  if (!out[0])
    param(f, key, out, n);
}
typedef struct {
  char query[2048], form[4096], app[64], title[160], text[1024], tok[80];
} notify_buf_t;
static esp_err_t notify_fill(httpd_req_t *r, notify_buf_t *b) {
  size_t ql = httpd_req_get_url_query_len(r);
  if (ql && ql < sizeof b->query)
    httpd_req_get_url_query_str(r, b->query, sizeof b->query);
  cJSON *j = NULL;
  if (r->method == HTTP_POST && r->content_len > 0) {
    if (r->content_len >= (int)sizeof b->form)
      return error(r, "413 Payload Too Large", "Nachricht zu lang");
    int n = 0;
    while (n < r->content_len) {
      int k = httpd_req_recv(r, b->form + n, r->content_len - n);
      if (k <= 0)
        return error(r, "400 Bad Request", "Anfrage unvollständig");
      n += k;
    }
    b->form[n] = 0;
    if (b->form[0] == '{')
      j = cJSON_Parse(b->form);
  }
  const char *f = j ? "" : b->form;
  field(j, b->query, f, "token", b->tok, sizeof b->tok);
  if (!b->tok[0] &&
      httpd_req_get_hdr_value_str(r, "Authorization", b->tok, sizeof b->tok) ==
          ESP_OK &&
      !strncmp(b->tok, "Bearer ", 7))
    memmove(b->tok, b->tok + 7, strlen(b->tok + 7) + 1);
  if (!notify_token_ok(b->tok)) {
    cJSON_Delete(j);
    return error(r, "401 Unauthorized", "Token fehlt oder ist falsch");
  }
  field(j, b->query, f, "app", b->app, sizeof b->app);
  field(j, b->query, f, "title", b->title, sizeof b->title);
  field(j, b->query, f, "text", b->text, sizeof b->text);
  cJSON_Delete(j);
  if (!b->app[0] && !b->title[0] && !b->text[0])
    return error(r, "400 Bad Request", "app, title oder text fehlt");
  const char *why;
  if (!notify_push(b->app, b->title, b->text, &why) && strcmp(why, "doppelt"))
    return error(r, "403 Forbidden", why);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "ok", true);
  if (why[0])
    cJSON_AddStringToObject(o, "note", why);
  return json(r, o);
}
static esp_err_t notify_handler(httpd_req_t *r) {
  /* ~7 KB nur waehrend der Anfrage belegen */
  notify_buf_t *b = calloc(1, sizeof *b);
  if (!b)
    return error(r, "503 Service Unavailable", "Speicher voll");
  esp_err_t e = notify_fill(r, b);
  free(b);
  return e;
}
static esp_err_t get(httpd_req_t *r) {
  const char *u = r->uri;
  if (!strcmp(u, "/") || !strcmp(u, "/index.html")) {
    headers(r);
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, (const char *)index_start,
                           index_end - index_start - 1);
  }
  if (!strcmp(u, "/app.js")) {
    headers(r);
    httpd_resp_set_type(r, "text/javascript; charset=utf-8");
    return httpd_resp_send(r, (const char *)js_start, js_end - js_start - 1);
  }
  if (!strcmp(u, "/style.css")) {
    headers(r);
    httpd_resp_set_type(r, "text/css; charset=utf-8");
    return httpd_resp_send(r, (const char *)css_start, css_end - css_start - 1);
  }
  if (is_notify(u))
    return notify_handler(r);
  if (strncmp(u, "/api/", 5)) {
    if (net_setup_active()) {
      httpd_resp_set_status(r, "302 Found");
      httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
      return httpd_resp_send(r, "", 0);
    }
    return error(r, "404 Not Found", "Nicht gefunden");
  }
  if (!authorized(r))
    return error(r, "401 Unauthorized", "Bitte mit dem Display koppeln");
  if (!strcmp(u, "/api/v1/config"))
    return json(r, config_public());
  if (!strcmp(u, "/api/v1/notify/config"))
    return json(r, notify_public());
  if (!strcmp(u, "/api/v1/notify/flow")) {
    /* Automate-Flow mit Adresse und Token genau dieses Boards */
    if (!net_connected())
      return error(r, "409 Conflict", "Board ist nicht im Heimnetz");
    char ip[32], url[128];
    net_address(ip, sizeof ip);
    int n = snprintf(url, sizeof url, "http://%s/api/v1/notify?token=%s", ip,
                     notify_token());
    if (n <= 0 || n >= 128) /* Laenge muss in ein Byte passen */
      return error(r, "500 Internal Server Error", "Adresse zu lang");
    unsigned char len = (unsigned char)n;
    headers(r);
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Content-Disposition",
                       "attachment; filename=\"Newsboard.flo\"");
    httpd_resp_send_chunk(r, (const char *)automate_flow_pre,
                          automate_flow_pre_len);
    httpd_resp_send_chunk(r, (const char *)&len, 1);
    httpd_resp_send_chunk(r, url, n);
    httpd_resp_send_chunk(r, (const char *)automate_flow_post,
                          automate_flow_post_len);
    return httpd_resp_send_chunk(r, NULL, 0);
  }
  if (!strcmp(u, "/api/v1/ota/status")) {
    static const char *names[] = {"idle", "checking", "downloading", "done", "error"};
    ota_online_t st;
    ota_online_status(&st);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", names[st.state]);
    cJSON_AddNumberToObject(o, "progress", st.progress);
    cJSON_AddStringToObject(o, "error", st.error);
    cJSON_AddStringToObject(o, "latest", st.latest);
    return json(r, o);
  }
  if (!strcmp(u, "/api/v1/modules"))
    return json(r, modules_public());
  if (!strcmp(u, "/api/v1/status")) {
    cJSON *o = net_status();
    cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(o, "partition", ota_partition_label());
    cJSON_AddBoolToObject(o, "paused", screen_paused());
    cJSON_AddNumberToObject(o, "heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(o, "minHeap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(o, "largestHeapBlock",
                            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(o, "uptime", esp_timer_get_time() / 1000000);
    static const char *const resets[] = {
        "?",        "power-on", "extern", "software", "PANIC",
        "int-wdt",  "task-wdt", "wdt",    "deepsleep", "brownout", "sdio"};
    esp_reset_reason_t why = esp_reset_reason();
    cJSON_AddStringToObject(o, "reset",
                            why < sizeof resets / sizeof *resets ? resets[why]
                                                                 : "?");
    cJSON_AddItemToObject(o, "calendars", calendar_status());
    return json(r, o);
  }
  return error(r, "404 Not Found", "Nicht gefunden");
}
static esp_err_t post(httpd_req_t *r) {
  if (is_notify(r->uri))
    return notify_handler(r);
  if (!same_origin(r))
    return error(r, "403 Forbidden", "Anfrage von fremder Webseite abgelehnt");
  const char *u = r->uri;
  if (!strcmp(u, "/api/v1/pair/request")) {
    screen_show_pair();
    return ok(r);
  }
  if (!strcmp(u, "/api/v1/pair")) {
    if (locked_until > esp_timer_get_time())
      return error(r, "429 Too Many Requests", "Bitte eine Minute warten");
    cJSON *j = body(r);
    const cJSON *pin = cJSON_GetObjectItemCaseSensitive(j, "code");
    char actual[7];
    screen_pair_code(actual);
    bool valid =
        cJSON_IsString(pin) && actual[0] && !strcmp(pin->valuestring, actual);
    cJSON_Delete(j);
    if (!valid) {
      if (++attempts >= 5) {
        locked_until = esp_timer_get_time() + 60000000;
        attempts = 0;
      }
      return error(r, "401 Unauthorized", "Code falsch oder abgelaufen");
    }
    attempts = 0;
    int slot = next_session++ % 3;
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof bytes);
    for (int i = 0; i < 16; i++)
      snprintf(sessions[slot].token + 2 * i, 3, "%02x", bytes[i]);
    nvs_handle_t h;
    if (nvs_open("newsboard", NVS_READWRITE, &h) == ESP_OK) {
      nvs_set_blob(h, "sessions", sessions, sizeof sessions);
      nvs_set_i32(h, "next_session", next_session);
      nvs_commit(h);
      nvs_close(h);
    }
    char cookie[160];
    snprintf(cookie, sizeof cookie,
             "nb_session=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000",
             sessions[slot].token);
    httpd_resp_set_hdr(r, "Set-Cookie", cookie);
    screen_command("closeSetup");
    return ok(r);
  }
  if (!authorized(r))
    return error(r, "401 Unauthorized", "Bitte erneut koppeln");
  /* Firmware-Upload streamt den Body selbst: vor body() (max. 8 KB) abfangen */
  if (!strcmp(u, "/api/v1/ota"))
    return ota_upload(r);
  if (!strcmp(u, "/api/v1/ota/check")) {
    char err[96];
    if (ota_online_check(err, sizeof err) != ESP_OK)
      return error(r, "502 Bad Gateway", err);
    ota_online_t st;
    ota_online_status(&st);
    char cur[32];
    snprintf(cur, sizeof cur, "%s", esp_app_get_description()->version);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "latest", st.latest);
    cJSON_AddStringToObject(o, "current", cur);
    cJSON_AddBoolToObject(o, "upToDate", ota_online_up_to_date(st.latest));
    cJSON_AddStringToObject(o, "asset", ota_online_asset());
    return json(r, o);
  }
  if (!strcmp(u, "/api/v1/ota/online")) {
    char err[96], tag[24] = "";
    if (r->content_len > 0) {
      cJSON *t = body(r);
      const cJSON *v = t ? cJSON_GetObjectItemCaseSensitive(t, "tag") : NULL;
      if (cJSON_IsString(v) && strlen(v->valuestring) < sizeof tag)
        snprintf(tag, sizeof tag, "%s", v->valuestring);
      cJSON_Delete(t);
    }
    esp_err_t e = ota_online_start(tag, err, sizeof err);
    if (e == ESP_OK)
      return ok(r);
    return error(r, e == ESP_ERR_INVALID_STATE ? "409 Conflict"
                                                : "400 Bad Request", err);
  }
  if (!strcmp(u, "/api/v1/wifi/scan"))
    return json(r, net_scan());
  cJSON *j = body(r);
  if (!j)
    return error(r, "400 Bad Request", "Ungültiges JSON oder Anfrage zu groß");
  char message[128] = "";
  bool success = false;
  if (!strcmp(u, "/api/v1/config")) {
    success = config_update(j, message, sizeof message);
    if (success)
      screen_command("config");
  } else if (!strcmp(u, "/api/v1/wifi")) {
    cJSON *s = cJSON_GetObjectItemCaseSensitive(j, "ssid"),
          *p = cJSON_GetObjectItemCaseSensitive(j, "password");
    success = cJSON_IsString(s) && cJSON_IsString(p) &&
              net_connect(s->valuestring, p->valuestring);
    if (!success)
      snprintf(message, sizeof message,
               "WLAN-Daten ungültig oder Verbindung läuft bereits");
  } else if (!strcmp(u, "/api/v1/playback")) {
    cJSON *c = cJSON_GetObjectItemCaseSensitive(j, "command");
    success = cJSON_IsString(c) && screen_command(c->valuestring);
  } else if (!strcmp(u, "/api/v1/notify/config")) {
    success = notify_update(j);
  } else if (!strcmp(u, "/api/v1/notify/test")) {
    const char *why;
    success = notify_push("WhatsApp", "Newsboard",
                          "Testnachricht vom Board. Wenn du das liest, klappt "
                          "die Anzeige.",
                          &why);
    if (!success)
      snprintf(message, sizeof message, "%s",
               why[0] ? why : "Test fehlgeschlagen");
  } else if (!strcmp(u, "/api/v1/calendars/check")) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "slot");
    success = cJSON_IsNumber(v) && v->valuedouble == v->valueint &&
              v->valueint >= 0 && v->valueint < CALENDAR_COUNT;
    if (success)
      calendar_request_check(v->valueint);
  }
  cJSON_Delete(j);
  return success ? ok(r)
                 : error(r, "400 Bad Request",
                         message[0] ? message : "Ungültige Anfrage");
}
bool web_start(void) {
  notify_init();
  nvs_handle_t h;
  if (nvs_open("newsboard", NVS_READONLY, &h) == ESP_OK) {
    size_t n = sizeof sessions;
    if (nvs_get_blob(h, "sessions", sessions, &n) != ESP_OK ||
        n != sizeof sessions)
      memset(sessions, 0, sizeof sessions);
    for (int i = 0; i < 3; i++)
      sessions[i].token[32] = 0;
    nvs_get_i32(h, "next_session", &next_session);
    nvs_close(h);
  }
  httpd_handle_t server = NULL;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 16384;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.lru_purge_enable = true;
  cfg.max_open_sockets = 4;
  cfg.recv_wait_timeout = 5;
  cfg.send_wait_timeout = 5;
  if (httpd_start(&server, &cfg) != ESP_OK)
    return false;
  httpd_uri_t g = {.uri = "/*", .method = HTTP_GET, .handler = get},
              p = {.uri = "/*", .method = HTTP_POST, .handler = post};
  httpd_register_uri_handler(server, &g);
  httpd_register_uri_handler(server, &p);
  return true;
}
