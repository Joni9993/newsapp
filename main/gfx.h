/* Zeichnen in streifenweise gerenderte Bilder.
 *
 * Die Welt ist 107x80 "Pixel" gross, jedes davon wird auf dem Display 3x3 gross (die letzte
 * Spalte nur zu zwei Dritteln - 320 ist nicht durch 3 teilbar).
 * So haben alle Pixel - Sprites, Partikel, Schrift - dieselbe Groesse, wie bei echter Pixel-Art.
 *
 * Gerendert wird in Streifen von GFX_BAND Zeilen: fuer jeden Streifen wird die ganze Szene
 * gezeichnet (alles ausserhalb wird abgeschnitten), dann geht er per DMA ans Display, waehrend
 * schon der naechste entsteht. Ein voller Bildspeicher (150 KB) waere fuer den C6 zu viel.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sprites.h"

#define GSCALE 3
#define GW 107
#define GH 80
#define GFX_BAND 8

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

enum {
    SPR_FLIP  = 1,    /* horizontal gespiegelt */
    SPR_WHITE = 2,    /* nur Silhouette in Weiss (Treffer-Blitz) */
    SPR_RED   = 4,    /* rot getoent */
    SPR_DARK  = 8,    /* halb so hell */
    SPR_GHOST = 16,   /* jedes zweite Pixel (Verblassen) */
    SPR_SHADOW = 32,  /* nur Silhouette in Fast-Schwarz (fuer Umrisse) */
    SPR_ICE   = 64,   /* eingefroren: blau-weiss */
    SPR_VIOLET = 128, /* Schatten: violett */
};

/* Ein Licht fuer den Beleuchtungsdurchgang; Farbe in 0..256 je Kanal. */
typedef struct { int16_t x, y, r; uint16_t cr, cg, cb; } light_t;

void gfx_band_begin(int y0);          /* Streifen ab Weltzeile y0 */
int  gfx_band_y0(void);
void gfx_band_end(void);              /* hochskalieren und losschicken */

void gfx_px(int x, int y, uint16_t c);
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
void gfx_circle(int cx, int cy, int r, uint16_t c);
void gfx_spr(const spr_t *s, int x, int y, int flags);
/* Dreht s um den Punkt (ox,oy) im Sprite, der bei (px,py) landet. ang: 0..1023 = 0..360 Grad. */
void gfx_spr_rot(const spr_t *s, int px, int py, int ox, int oy, int ang, int flags);
/* Dasselbe mit Massstab: scale 256 = 1:1, 384 = 1,5-fach. */
void gfx_spr_rot_s(const spr_t *s, int px, int py, int ox, int oy, int ang, int scale, int flags);
void gfx_spr_rot_outline_s(const spr_t *s, int px, int py, int ox, int oy, int ang, int scale, int flags);
/* Sprite mit dunklem 1-Pixel-Umriss - hebt den Helden aus dem Getuemmel heraus. */
void gfx_spr_outline(const spr_t *s, int x, int y, int flags);
/* Vergroessert ohne Drehung (x0,y0 = oben links); schnell auch fuer grosse Figuren */
void gfx_spr_scaled(const spr_t *s, int x0, int y0, int scale, int flags);
void gfx_spr_scaled_outline(const spr_t *s, int x0, int y0, int scale, int flags);
void gfx_outline_color(uint16_t c);   /* Farbe fuer SPR_SHADOW und Umrisse */
void gfx_spr_rot_outline(const spr_t *s, int px, int py, int ox, int oy, int ang, int flags);
void gfx_text(int x, int y, const char *t, uint16_t c);   /* 3x5-Schrift mit Schatten */
void gfx_text_plain(int x, int y, const char *t, uint16_t c);   /* ohne Schatten */
void gfx_text_big(int x, int y, const char *t, uint16_t c, int k);   /* k-fach vergroessert, Breite 4*k je Zeichen */
int  gfx_text_w(const char *t);

/* Beleuchtung fuer den aktuellen Streifen: Grundlicht je Kanal 0..256, Lichter addieren dazu,
 * vign 0..256 dunkelt den Rand ab, redpulse 0..256 faerbt den Rand rot. */
void gfx_light(int ar, int ag, int ab, const light_t *l, int n, int vign, int redpulse);

int  isin(int a);   /* a: 0..1023, Ergebnis Q12 (-4096..4096) */
int  icos(int a);
