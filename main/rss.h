/* Stueckweiser RSS/Atom-Leser: meldet je <item> bzw. <entry> Titel und Beschreibung als reinen Text
 * (CDATA, Entities und HTML-Tags sind aufgeloest). Gibt der Rueckruf false zurueck, wird abgebrochen. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define RSS_TITLE_MAX 200
#define RSS_DESC_MAX  400
#define RSS_RAW_MAX   1200

typedef bool (*rss_cb)(const char *title, const char *desc, void *ctx);

typedef struct {
    rss_cb cb;
    void *ctx;
    int mode;                /* Text / Tag / CDATA / Kommentar */
    bool in_item, stopped;
    int field;               /* 0 = keins, 1 = Titel, 2 = Beschreibung */
    char tag[40];
    int tlen;
    char last[3];            /* die letzten Zeichen, fuer "]]>" und "-->" */
    char raw[2][RSS_RAW_MAX];
    int rlen[2];
} rss_t;

void rss_init(rss_t *r, rss_cb cb, void *ctx);
bool rss_feed(rss_t *r, const char *data, size_t n);
