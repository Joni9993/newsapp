#include "net.h"
#include "config.h"
#include "screen.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile bool connected, ap_active, trying, save_pending;
static esp_netif_t *sta;
static char ap_name[32], ap_pass[17], pending_ssid[33], pending_pass[65],
    error[96];
static int64_t next_retry, attempt_started, ap_stop;
static unsigned failures;
static uint8_t last_reason;
static SemaphoreHandle_t guard;
bool net_connected(void) { return connected; }
bool net_time_valid(void) { return time(NULL) > 1700000000; }
bool net_setup_active(void) { return ap_active; }
bool net_configured(void) {
  board_config_t c;
  config_get(&c);
  return c.ssid[0] != 0;
}
const char *net_ap_name(void) { return ap_name; }
const char *net_ap_password(void) { return ap_pass; }
void net_address(char *out, size_t n) {
  esp_netif_ip_info_t ip = {0};
  if (sta)
    esp_netif_get_ip_info(sta, &ip);
  snprintf(out, n, IPSTR, IP2STR(&ip.ip));
}
static void station_config(const char *s, const char *p) {
  wifi_config_t w = {0};
  memcpy(w.sta.ssid, s, strlen(s));
  memcpy(w.sta.password, p, strlen(p));
  /* Alle Kanaele absuchen und den staerksten Zugangspunkt nehmen (Mesh/Repeater
   * mit gleichem Namen), WPA3 mit beiden SAE-Verfahren erlauben. */
  w.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  w.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  w.sta.failure_retry_cnt = 2;
  w.sta.pmf_cfg.capable = true;
  w.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  esp_wifi_set_config(WIFI_IF_STA, &w);
}
/* Klartext zum letzten Trennungsgrund des WLAN-Treibers */
static const char *reason_text(uint8_t r) {
  switch (r) {
  case 0:
    return "Keine Antwort vom Router.";
  case WIFI_REASON_NO_AP_FOUND:
  case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
    return "Netz nicht gefunden. Name exakt? Nur 2,4 GHz wird unterstützt.";
  case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
  case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    return "Verschlüsselung passt nicht (offenes Netz oder nur WPA3?).";
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_MIC_FAILURE:
  case WIFI_REASON_802_1X_AUTH_FAILED:
    return "Passwort falsch? Groß-/Kleinschreibung prüfen.";
  case WIFI_REASON_ASSOC_TOOMANY:
    return "Router lehnt weitere Geräte ab.";
  case WIFI_REASON_BEACON_TIMEOUT:
    return "Signal zu schwach. Board näher an den Router.";
  default:
    return "Router hat die Verbindung abgelehnt.";
  }
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  xSemaphoreTake(guard, portMAX_DELAY);
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    connected = false;
    last_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    ESP_LOGW("wlan", "getrennt, Grund %u", last_reason);
    failures++;
    next_retry = esp_timer_get_time() +
                 ((failures > 5 ? 30 : failures * 2) + 1) * 1000000LL;
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    connected = true;
    failures = 0;
    last_reason = 0;
    error[0] = 0;
    /* Speichern erst im WLAN-Task: der System-Event-Task hat nur ~2 KB Stack */
    if (trying)
      save_pending = true;
    if (ap_active)
      ap_stop = esp_timer_get_time() + 120000000LL;
    esp_netif_sntp_start();
  }
  xSemaphoreGive(guard);
}
void net_recovery(void) {
  xSemaphoreTake(guard, portMAX_DELAY);
  if (!ap_active) {
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    ap_active = true;
  }
  ap_stop = 0;
  xSemaphoreGive(guard);
}
bool net_connect(const char *s, const char *p) {
  if (!s || !p || !s[0] || strlen(s) > 32 || strlen(p) > 63 ||
      (p[0] && strlen(p) < 8))
    return false;
  xSemaphoreTake(guard, portMAX_DELAY);
  if (trying) {
    xSemaphoreGive(guard);
    return false;
  }
  snprintf(pending_ssid, sizeof pending_ssid, "%s", s);
  snprintf(pending_pass, sizeof pending_pass, "%s", p);
  trying = true;
  connected = false;
  error[0] = 0;
  last_reason = 0;
  attempt_started = esp_timer_get_time();
  ap_stop = 0;
  esp_wifi_disconnect();
  station_config(s, p);
  next_retry = attempt_started + 500000;
  xSemaphoreGive(guard);
  return true;
}
static void maintain(void *arg) {
  for (;;) {
    xSemaphoreTake(guard, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (save_pending) {
      save_pending = false;
      trying = false;
      if (!config_wifi(pending_ssid, pending_pass))
        snprintf(error, sizeof error,
                 "WLAN verbunden, Speichern fehlgeschlagen");
      memset(pending_pass, 0, sizeof pending_pass);
      /* Display zeigt jetzt die neue Adresse im Heimnetz und einen Code */
      screen_show_pair();
    }
    if (trying && !connected && now - attempt_started > 40000000) {
      trying = false;
      snprintf(error, sizeof error, "Fehlgeschlagen (Code %u): %s", last_reason,
               reason_text(last_reason));
      memset(pending_pass, 0, sizeof pending_pass);
      board_config_t c;
      config_get(&c);
      esp_wifi_disconnect();
      station_config(c.ssid, c.password);
    }
    if (!connected && now >= next_retry && (trying || net_configured())) {
      esp_wifi_connect();
      next_retry = now + 30000000;
    }
    if (ap_active && connected && ap_stop && now > ap_stop) {
      esp_wifi_set_mode(WIFI_MODE_STA);
      ap_active = false;
      ap_stop = 0;
    }
    xSemaphoreGive(guard);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
static void dns_task(void *arg) {
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    vTaskDelete(NULL);
    return;
  }
  struct sockaddr_in addr = {.sin_family = AF_INET,
                             .sin_port = htons(53),
                             .sin_addr.s_addr = INADDR_ANY};
  bind(fd, (struct sockaddr *)&addr, sizeof addr);
  uint8_t b[512];
  for (;;) {
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    int n = recvfrom(fd, b, sizeof b - 16, 0, (struct sockaddr *)&from, &fl);
    if (n < 17 || !ap_active || (ntohl(from.sin_addr.s_addr) >> 8) != 0xc0a804)
      continue;
    if (b[4] != 0 || b[5] != 1)
      continue;
    int p = 12;
    while (p < n && b[p]) {
      if (b[p] > 63) {
        p = n;
        break;
      }
      p += b[p] + 1;
    }
    p++;
    if (p + 4 > n || b[p] != 0 || b[p + 1] != 1 || b[p + 2] != 0 ||
        b[p + 3] != 1)
      continue;
    n = p + 4;
    b[2] = 0x81;
    b[3] = 0x80;
    b[6] = 0;
    b[7] = 1;
    memset(b + 8, 0, 4);
    const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0,   1,   0, 0,
                              0,    30,   0, 4, 192, 168, 4, 1};
    memcpy(b + n, answer, sizeof answer);
    sendto(fd, b, n + sizeof answer, 0, (struct sockaddr *)&from, fl);
  }
}
void net_start(void) {
  guard = xSemaphoreCreateMutex();
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(ap_name, sizeof ap_name, "NEWSBOARD-%02X%02X", mac[4], mac[5]);
  uint8_t r[8];
  esp_fill_random(r, sizeof r);
  for (int i = 0; i < 8; i++)
    snprintf(ap_pass + 2 * i, 3, "%02X", r[i]);
  gpio_config_t g = {.pin_bit_mask = (1ULL << 3) | (1ULL << 14),
                     .mode = GPIO_MODE_OUTPUT};
  gpio_config(&g);
  gpio_set_level(3, 0);
  gpio_set_level(14, 0);
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  esp_netif_init();
  esp_event_loop_create_default();
  sta = esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t w = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&w);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL);
  board_config_t c;
  config_get(&c);
  ap_active = !c.ssid[0];
  esp_wifi_set_mode(ap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  wifi_config_t ap = {0};
  strcpy((char *)ap.ap.ssid, ap_name);
  strcpy((char *)ap.ap.password, ap_pass);
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap.ap.max_connection = 3;
  ap.ap.channel = 1;
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  station_config(c.ssid, c.password);
  esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  sc.start = false;
  esp_netif_sntp_init(&sc);
  esp_wifi_start();
  esp_wifi_set_protocol(WIFI_IF_STA,
                        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  xTaskCreate(maintain, "wlan", 8192, NULL, 2, NULL);
  xTaskCreate(dns_task, "portal-dns", 3072, NULL, 2, NULL);
}
cJSON *net_status(void) {
  cJSON *o = cJSON_CreateObject();
  char ip[32];
  net_address(ip, sizeof ip);
  xSemaphoreTake(guard, portMAX_DELAY);
  cJSON_AddBoolToObject(o, "connected", connected);
  cJSON_AddBoolToObject(o, "setup", ap_active);
  cJSON_AddBoolToObject(o, "connecting", trying);
  cJSON_AddStringToObject(o, "ip", ip);
  cJSON_AddStringToObject(o, "error", error);
  cJSON_AddNumberToObject(o, "reason", last_reason);
  wifi_ap_record_t ap;
  if (connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    cJSON_AddNumberToObject(o, "rssi", ap.rssi);
    cJSON_AddNumberToObject(o, "channel", ap.primary);
  }
  cJSON_AddBoolToObject(o, "timeValid", net_time_valid());
  xSemaphoreGive(guard);
  return o;
}
cJSON *net_scan(void) {
  cJSON *a = cJSON_CreateArray();
  if (trying)
    return a;
  wifi_scan_config_t scan = {0};
  if (esp_wifi_scan_start(&scan, true) != ESP_OK)
    return a;
  uint16_t n = 20;
  wifi_ap_record_t *records = calloc(n, sizeof *records);
  if (!records) {
    esp_wifi_clear_ap_list();
    return a;
  }
  esp_wifi_scan_get_ap_records(&n, records);
  for (int i = 0; i < n; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", (char *)records[i].ssid);
    cJSON_AddNumberToObject(o, "rssi", records[i].rssi);
    cJSON_AddBoolToObject(o, "open", records[i].authmode == WIFI_AUTH_OPEN);
    cJSON_AddNumberToObject(o, "channel", records[i].primary);
    cJSON_AddItemToArray(a, o);
  }
  free(records);
  return a;
}
int net_get(const char *url, net_data_cb cb, void *ctx) {
  return net_get_ex(url, cb, ctx, 1024 * 1024, 45);
}
int net_get_ex(const char *url, net_data_cb cb, void *ctx, size_t max_bytes,
               int max_seconds) {
  if (!connected)
    return -1;
  if (!strncmp(url, "https://", 8) && !net_time_valid())
    return -2;
  esp_http_client_config_t cfg = {.url = url,
                                  .user_agent = "Newsboard/1.0 (ESP32)",
                                  .timeout_ms = 10000,
                                  .buffer_size = 2048,
                                  .buffer_size_tx = 1024,
                                  .crt_bundle_attach = esp_crt_bundle_attach,
                                  .disable_auto_redirect = true};
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c)
    return -1;
  int status = -1;
  int64_t start = esp_timer_get_time();
  for (int hop = 0; hop < 4; hop++) {
    if (esp_http_client_open(c, 0) != ESP_OK)
      break;
    int64_t length = esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    if (status >= 300 && status < 400) {
      /* Rest der Weiterleitungsantwort lesen, sonst landet er in der naechsten */
      esp_http_client_flush_response(c, NULL);
      if (esp_http_client_set_redirection(c) != ESP_OK) {
        ESP_LOGW("net", "Weiterleitung %d ohne gueltiges Ziel", status);
        status = -1;
        break;
      }
      char target[1024];
      esp_http_client_get_url(c, target, sizeof target);
      if (!strncmp(url, "https://", 8) && strncmp(target, "https://", 8)) {
        status = -4;
        break;
      }
      esp_http_client_close(c);
      continue;
    }
    if (status == 200) {
      char buf[1024];
      int n = 0;
      size_t total = 0;
      bool stopped = false;
      while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0) {
        total += n;
        if (total > max_bytes ||
            esp_timer_get_time() - start > max_seconds * 1000000LL) {
          status = -3;
          break;
        }
        if (!cb(buf, n, ctx)) {
          stopped = true;
          break;
        }
        vTaskDelay(1);
      }
      if (n < 0 || (!stopped && status == 200 &&
                    /* bei chunked ist "length" nur die Groesse des ersten Stuecks */
                    ((length > 0 && !esp_http_client_is_chunked_response(c) &&
                      total != length) ||
                     !esp_http_client_is_complete_data_received(c)))) {
        ESP_LOGW("net", "unvollstaendig: hop %d read %d total %u laenge %lld",
                 hop, n, (unsigned)total, (long long)length);
        status = -1;
      }
    }
    break;
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return status;
}
