#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#define MODULE_COUNT 18
#define CALENDAR_COUNT 6   /* Obergrenze: der NVS-Speicher (24 KB) haelt die Config als ein Blob */
#define CAL_URL_MAX 768
typedef struct { char name[48], provider[12], url[CAL_URL_MAX]; } calendar_config_t;
typedef struct {
    uint32_t version;
    char ssid[33], password[65], city[64];
    uint8_t region, mode, theme, brightness, night_brightness, night_start, night_end;
    uint16_t duration;
    bool enabled[MODULE_COUNT];
    uint8_t order[MODULE_COUNT];
    calendar_config_t calendars[CALENDAR_COUNT];
} board_config_t;
bool config_init(void);
void config_get(board_config_t *out);
/* Einzelwerte ohne Kopie: die ganze Config ist mit 6 Kalendern ~5 KB, zu viel fuer kleine Stacks */
bool config_enabled(int module);
int config_order(int pos);
uint16_t config_duration(void);
bool config_has_ssid(void);
void config_credentials(char ssid[33], char password[65]);
void config_calendar(int slot, calendar_config_t *out);
void config_location(char *city, size_t size, uint8_t *region);
uint32_t config_revision(void);
cJSON *config_public(void);
bool config_update(const cJSON *json, char *error, size_t size);
bool config_wifi(const char *ssid, const char *password);
bool config_url_valid(const char *url);
/* NULL wenn gueltig, sonst ein Grund fuer die Webseite */
const char *config_url_problem(const char *url);
void config_defaults(board_config_t *out);
