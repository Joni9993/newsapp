/* Die Welt hinter und unter den Nachrichten: Himmel nach Tageszeit und echtem Wetter,
 * Sonne und Mond auf ihrer Bahn, Huegel, eine Wiese im Wind, ein Baum mit Jahreszeiten,
 * der gelbe Briefkasten der Eule, Voegel, Schmetterlinge, Gluehwuermchen, Regen und Schnee. */
#pragma once
#include <stdint.h>

#define SCENE_TOP     8     /* unter der Kopfzeile */
#define SCENE_GROUND  66    /* Oberkante der Wiese */

void scene_tick(void);
void scene_draw_back(void);    /* Himmel, Gestirne, Wolken, Huegel, Baum - hinter den Nachrichten */
void scene_draw_front(void);   /* Wiese, Blumen, Briefkasten, Niederschlag, Tiere - davor */
void scene_draw_topbar(void);

/* Landeplaetze der Eule: 0 = Briefkasten rechts, 1 = Baumstumpf links (Fusspunkt) */
void scene_perch(int which, float *x, float *y);

/* 0 = Landschaft mit Eule, 1 = zerfallene Stadt mit Roboter */
void scene_set_theme(int theme);
