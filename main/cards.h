/* Karten: eine Nachricht oder eine Tafel. Die Quellen legen sie hier ab, die Eule holt sie ab.
 *
 * Textkarten bestehen aus bis zu vier Teilen (z.B. Dachzeile, Titel, Text), jeder mit eigener Farbe;
 * umgebrochen wird erst beim Anzeigen. Tafeln (Wetter, Fakten) zeichnet die Eule als Ganzes.
 *
 * Jede Karte hat eine Kennung aus ihrem Inhalt. Die Ablage merkt sich, was wann gezeigt wurde,
 * und bietet zuerst Ungesehenes an - Gesehenes kommt spaeter trotzdem wieder dran.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SRC_HALLO, SRC_NEWS, SRC_WETTER, SRC_WIKI_HEUTE, SRC_WIKI_GEBURT, SRC_WIKI_ZUFALL,
 SRC_SUN, SRC_MOON, SRC_DATE, SRC_ENERGY, SRC_AIR, SRC_QUAKE, SRC_HOLIDAY, SRC_SPACE,
 SRC_BITCOIN, SRC_EXCHANGE, SRC_CALENDAR, SRC_CLOCK, SRC_COUNT };
#define SRC_FAKT SRC_SUN
enum { CARD_TEXT, CARD_WEATHER, CARD_FACT };

/* Symbole fuer Fakt-Tafeln */
enum { ICON_SUN, ICON_MOON, ICON_LEAF, ICON_AIR, ICON_QUAKE, ICON_PARTY, ICON_CAL, ICON_ROCKET, ICON_COIN };

#define CARD_TEXT_MAX 600
#define CARD_W_MAX    24     /* Zeichen je Zeile eines Textbausteins */
#define CARD_PARTS    4

typedef struct {
    uint8_t kind, src, nparts, icon;
    char head[28];
    char big[12];                       /* Fakt-Tafel: grosse Zahl, z.B. "62%" */
    struct { uint16_t color, off, len; } part[CARD_PARTS];
    char text[CARD_TEXT_MAX];
    char mood[8];                       /* kurzer Ausruf der Eule */
    uint32_t id;
} card_t;

void cards_init(void);
void cards_set(int src, const card_t *list, int n);     /* alle Karten einer Quelle ersetzen */
void cards_push(int src, const card_t *c);              /* anhaengen; die aelteste faellt raus */
bool cards_next(card_t *out);                           /* naechste Karte nach Wechselplan */
void cards_previous(void);
void cards_replay(void);
void cards_filter(void);
int  cards_count(int src);
int  cards_capacity(int src);

/* Bausteine */
void card_begin(card_t *c, int kind, int src, const char *head_utf8);
void card_part(card_t *c, const char *utf8, uint16_t color);   /* vereinfacht den Text (Umlaute ...) */
void card_finish(card_t *c);                                    /* Kennung berechnen */
