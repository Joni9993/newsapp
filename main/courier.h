/* Der Nachrichtenbote: eine Posteule (oder ein Roboter), der die Nachrichten Zeile fuer Zeile
 * als Foerderband ins Bild traegt und Wetter- und Fakten-Tafeln als Ganzes bringt. */
#pragma once

#define COURIER_HZ 30

void courier_init(void);
void courier_tick(void);
void courier_render(void);
void courier_skip(void);          /* aktuelle Karte sofort wegraeumen (Konsole) */
void courier_status(char *buf, int n);

/* Aussehen: 0 = Posteule in der Landschaft, 1 = Roboter in der zerfallenen Stadt */
void courier_set_theme(int theme);
int  courier_theme(void);

void courier_reset(void);

/* Push-Nachricht: der Bote bringt sie als Brief-Tafel (kind = NK_*) */
#include "cards.h"
#include <stdbool.h>
void courier_note(const card_t *card, int kind);
bool courier_note_active(void);
