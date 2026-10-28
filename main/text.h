/* Text fuer die 3x5-Pixelschrift: nur Grossbuchstaben, Ziffern und ein paar Zeichen.
 * Umlaute und ss behalten ein eigenes Zeichen (Codes unten), typografische Zeichen werden vereinfacht. */
#pragma once
#include <stddef.h>

/* Eigene Schriftzeichen fuer Ae, Oe, Ue und Eszett (1 Byte, unterhalb von Leerzeichen) */
#define TEXT_AE "\x01"
#define TEXT_OE "\x02"
#define TEXT_UE "\x03"
#define TEXT_SZ "\x04"
#define TEXT_DEG "\x05"   /* Gradzeichen (eigene Glyphe, nur fuer feste Texte) */

/* UTF-8 -> darstellbarer Text (Grossbuchstaben). Schneidet bei max-1 Zeichen ab. */
void text_simplify(char *out, size_t max, const char *utf8);

/* Bricht Text an Wortgrenzen auf hoechstens 'width' Zeichen je Zeile um.
 * Schreibt bis zu max_lines Zeilen nach lines (je line_size Bytes) und liefert ihre Anzahl.
 * Passt der Text nicht hinein, endet die letzte Zeile mit "...". */
int text_wrap(const char *text, int width, char *lines, int line_size, int max_lines);
