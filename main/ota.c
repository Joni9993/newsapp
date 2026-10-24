#include "ota.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ota"
/* Heap ist knapp (~150 KB): nur ein kleiner Puffer, nie das ganze Image */
#define CHUNK 4096

static bool busy;

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
  discard = false;
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
  if (begun)
    esp_ota_abort(h);
  if (discard)
    drain(r, buf, total - got);
  free(buf);
  busy = false;
  return reply(r, status, false, msg);
}
