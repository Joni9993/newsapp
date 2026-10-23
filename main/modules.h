#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "config.h"
enum { MOD_NEWS, MOD_WEATHER, MOD_HISTORY, MOD_BIRTHDAYS, MOD_RANDOM,
 MOD_SUN, MOD_MOON, MOD_DATE, MOD_ENERGY, MOD_AIR, MOD_QUAKE, MOD_HOLIDAY,
 MOD_SPACE, MOD_BITCOIN, MOD_EXCHANGE, MOD_CALENDAR, MOD_CLOCK, MOD_JW };
typedef struct { const char *id, *name; int interval; } module_t;
typedef struct { time_t last_success; bool loading; char error[96]; } module_status_t;
extern const module_t modules[MODULE_COUNT];
bool module_enabled(int id);
void module_begin(int id);
void module_end(int id, const char *error);
void module_status(int id, module_status_t *out);
int module_from_id(const char *id);
cJSON *modules_public(void);
