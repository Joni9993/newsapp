/* Text fuer die 3x5-Pixelschrift: nur Grossbuchstaben, Ziffern und ein paar Zeichen.
 * Umlaute werden ausgeschrieben (Ae, Oe, Ue, Ss), typografische Zeichen vereinfacht. */
#pragma once
#include <stddef.h>

/* UTF-8 -> darstellbarer Text (Grossbuchstaben). Schneidet bei max-1 Zeichen ab. */
void text_simplify(char *out, size_t max, const char *utf8);

/* Bricht Text an Wortgrenzen auf hoechstens 'width' Zeichen je Zeile um.
 * Schreibt bis zu max_lines Zeilen nach lines (je line_size Bytes) und liefert ihre Anzahl.
 * Passt der Text nicht hinein, endet die letzte Zeile mit "...". */
int text_wrap(const char *text, int width, char *lines, int line_size, int max_lines);
