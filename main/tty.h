/* Terminal-Dashboard (Anzeigemodus 1): gruene Phosphorschrift, Matrix-Regen im
 * Hintergrund und kleine Animationen, die die Live-Daten zeigen. */
#pragma once
#include "cards.h"
#include "notify.h"
#include <stdbool.h>

#define TTY_ROWS 9    /* Textzeilen auf dem Bildschirm */
#define TTY_COLS 25   /* Zeichen je Zeile */
#define TTY_CPS 30    /* Tippgeschwindigkeit in Zeichen je Sekunde */

/* Einmal je Bild aufrufen (Animationen weiterschalten) */
void tty_tick(void);
/* Zeichnet den aktuellen Streifen. card = NULL: noch keine Daten (Bootanzeige).
 * ticks zaehlt die Bilder seit Beginn der Seite, duration ist die Standzeit in Bildern. */
void tty_draw(const card_t *card, const char lines[][26], int line_count,
              unsigned page, unsigned ticks, unsigned duration, bool paused);
/* Push-Nachricht, die alles andere unterbricht (in beiden Anzeigemodi) */
void tty_draw_notify(const card_t *card, const char lines[][26], int n,
                     unsigned ticks, unsigned duration, const notify_t *nt);
