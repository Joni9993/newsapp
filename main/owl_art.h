/* Erzeugt von tools/mkchars.py - nicht von Hand bearbeiten.
 * Eule und Roboter haben dieselben Posen (OWL_*). */
#pragma once
#include "sprites.h"

enum { OWL_SITZT, OWL_BLINZELT, OWL_LINKS, OWL_RECHTS, OWL_FROH, OWL_STAUNT, OWL_FLUG_HOCH, OWL_FLUG_MITTE, OWL_FLUG_TIEF, OWL_SCHLAEFT, OWL_COUNT };
extern const spr_t owl[OWL_COUNT];
extern const spr_t robot[OWL_COUNT];
