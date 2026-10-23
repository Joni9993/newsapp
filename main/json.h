/* Stueckweiser JSON-Leser fuer grosse Antworten (die Tagesschau liefert ~200 KB).
 *
 * Er baut keinen Baum auf, sondern meldet jeden einfachen Wert mit seinem Schluessel und der Tiefe,
 * in der er steht (1 = oberstes Objekt). Werte in Listen kommen mit dem Schluessel der Liste
 * ("temperature_2m_max":[1,2] meldet zweimal temperature_2m_max), in namenlosen Listen leer.
 * Zeichenketten kommen entschluesselt als UTF-8 an (\", ü ...), Zahlen und true/false/null als
 * Text. Gibt der Rueckruf false zurueck, wird abgebrochen.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define JSON_VAL_MAX 700
#define JSON_DEPTH   32

typedef bool (*json_cb)(const char *key, const char *val, int depth, void *ctx);

typedef struct {
    json_cb cb;
    void *ctx;
    char stack[JSON_DEPTH];      /* '{' oder '[' je Ebene */
    char ckey[JSON_DEPTH][32];   /* Schluessel, unter dem die Ebene steht */
    int depth;
    int state;                   /* frei / in Zeichenkette / in Literal */
    bool esc, expect_key, have_key, stopped;
    int uni;                     /* noch zu lesende Hex-Ziffern von \uXXXX */
    unsigned ucode, uhigh;
    char key[48];
    char buf[JSON_VAL_MAX];
    int blen;
} json_t;

void json_init(json_t *j, json_cb cb, void *ctx);
/* Fuettert ein Stueck; liefert false, sobald abgebrochen wurde. */
bool json_feed(json_t *j, const char *data, size_t n);
