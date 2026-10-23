/* Die Quellen: holen Daten aus dem Netz und legen fertige Karten ab.
 * Alles laeuft in einer eigenen Aufgabe, die Anzeige wartet nie auf das Netz. */
#pragma once
#include <stdbool.h>

void sources_start(void);

/* Was Himmel, Landschaft und Wetter-Tafel brauchen */
typedef struct {
    bool valid;
    float temp, wind;
    int code;                 /* WMO-Wettercode von Open-Meteo */
    bool is_day;
    float tmin[2], tmax[2];   /* heute, morgen */
    int code_day[2];
    int rain[2];              /* Regenrisiko in % */
    int sunrise_min, sunset_min;   /* Minuten seit Mitternacht, 0 = unbekannt */
} weather_t;
void sources_weather(weather_t *out);
const char *sources_city(void);   /* "" solange unbekannt */
const char *sources_status(void); /* was gerade geholt wird - fuers Log */
