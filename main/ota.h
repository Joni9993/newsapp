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
