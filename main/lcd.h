/* ST7789, 2" 240x320, am XIAO ESP32C6 - quer betrieben (320x240). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define LCD_W 320
#define LCD_H 240

/* Pins wie im alten Projekt am Geraet bestaetigt (fliegende Draehte). */
#define LCD_PIN_SCK  19   /* D8  */
#define LCD_PIN_MOSI 18   /* D10 */
#define LCD_PIN_CS   21   /* D3  */
#ifdef LCD_LEGACY_PANEL   /* altes Board (Nachrichtenboard Posteule): DC/RST vertauscht, MADCTL 0x60, Farbumkehr an */
#define LCD_PIN_DC   2    /* D2 */
#define LCD_PIN_RST  1    /* D1 */
#define LCD_MADCTL   0x60
#define LCD_INVERT   0x21
#else
#define LCD_PIN_DC   1    /* D1: new 2.8 inch panel */
#define LCD_PIN_RST  2    /* D2 */
#define LCD_MADCTL   0xA0 /* new panel: landscape, rotated 180 degrees */
#define LCD_INVERT   0x20 /* new panel: inversion off */
#endif
#define LCD_PIN_BL   0    /* D0  */

bool lcd_init(void);
/* Startet ein neues Bild: das naechste lcd_push beginnt oben links. */
void lcd_frame_begin(void);
/* Haengt rows Zeilen (volle Breite, byteweise schon getauscht) an das Bild an.
 * Kehrt sofort zurueck; der Puffer darf erst nach lcd_wait() wieder beschrieben werden. */
void lcd_push(const uint16_t *buf, int rows);
/* Wartet, bis hoechstens 'pending' Uebertragungen offen sind. */
void lcd_wait(int pending);
/* Hintergrundlicht 0..100 % (PWM) */
void lcd_backlight(int percent);
