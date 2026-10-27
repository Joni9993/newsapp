/* Firmware-Update ueber die Weboberflaeche (OTA per Datei-Upload).
 * Das Board holt nichts selbst aus dem Netz: die .bin kommt vom Browser. */
#pragma once
#include "esp_http_server.h"

/* Bestaetigt das laufende Image, falls es gerade erst per OTA installiert
 * wurde (sonst Rollback beim naechsten Neustart). Aufrufen, sobald der
 * Webserver laeuft. */
void ota_mark_valid(void);
/* POST /api/v1/ota: Body ist das rohe .bin. Streamt in die andere
 * OTA-Partition, antwortet mit JSON und startet danach neu. Auth/Origin
 * muessen vorher geprueft sein. */
esp_err_t ota_upload(httpd_req_t *r);
/* Label der laufenden Partition (z. B. "ota_0"), "?" falls unbekannt */
const char *ota_partition_label(void);

/* ---- Online-Update (Board laedt das neueste GitHub-Release selbst) ---- */
typedef enum { OTA_IDLE, OTA_CHECKING, OTA_DOWNLOADING, OTA_DONE, OTA_ERROR } ota_state_t;
typedef struct {
  ota_state_t state;
  int progress; /* 0..100 */
  char latest[24];
  char error[128];
} ota_online_t;
void ota_online_status(ota_online_t *out);
/* Fragt GitHub in einer eigenen Aufgabe (kehrt sofort zurueck). Danach zeigt
 * ota_online_status(): state CHECKING, dann IDLE (latest gesetzt) oder ERROR.
 * ESP_ERR_INVALID_STATE: es laeuft schon ein Update/eine Pruefung. */
esp_err_t ota_online_check_start(char *err, size_t en);
const char *ota_online_asset(void);
bool ota_online_up_to_date(const char *tag);
/* Startet den Download-Task. tag leer/NULL -> zuletzt geprueftes Release */
esp_err_t ota_online_start(const char *tag, char *err, size_t en);
