#include "tty.h"
#include "esp_timer.h"
#include "gfx.h"
#include "net.h"
#include "ota.h"
#include "config.h"
#include "sources.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BG RGB(2, 8, 4)
#define HI RGB(57, 255, 136)
#define FG RGB(170, 255, 196)
#define DIM RGB(36, 118, 74)
#define DARK RGB(14, 52, 32)
#define AMBER RGB(255, 196, 64)
#define CYAN RGB(95, 215, 255)
#define WHITE RGB(225, 255, 235)
#define PINK RGB(255, 95, 180)

#define TOP 8       /* unter der Kopfzeile */
#define BOTTOM 78   /* darunter nur der Fortschrittsbalken */
#define RAIN_ROWS 12
#define RAIN_COLS 27

static unsigned frame;
static uint32_t seed = 0x9E3779B9u;
static struct {
  int16_t head;
  uint8_t len, speed, phase;
} rain[RAIN_COLS];
static weather_t wx;

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
static void drop(int c, bool anywhere) {
  rain[c].len = 4 + rnd() % 7;
  rain[c].speed = 2 + rnd() % 5;
  rain[c].phase = rnd() % 8;
  rain[c].head = anywhere ? (int)(rnd() % (RAIN_ROWS + 8)) - 4
                          : -(int)(rnd() % 14);
}
void tty_tick(void) {
  if (!frame)
    for (int c = 0; c < RAIN_COLS; c++)
      drop(c, true);
  frame++;
  for (int c = 0; c < RAIN_COLS; c++)
    if ((frame + rain[c].phase) % rain[c].speed == 0 &&
        ++rain[c].head - rain[c].len > RAIN_ROWS)
      drop(c, false);
  if (frame % 30 == 1)
    sources_weather(&wx);
}

/* ------------------------------------------------------------ Grundformen */

static bool band_hit(int y, int h) {
  int y0 = gfx_band_y0();
  return y + h > y0 && y < y0 + GFX_BAND;
}
static int isq(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v)
    r++;
  return r;
}
static void ell(int cx, int cy, int rx, int ry, uint16_t c) {
  if (!band_hit(cy - ry, 2 * ry + 1))
    return;
  for (int dy = -ry; dy <= ry; dy++) {
    int h = ry ? rx * isq(ry * ry - dy * dy) / ry : rx;
    gfx_fill(cx - h, cy + dy, 2 * h + 1, 1, c);
  }
}
static void disc(int cx, int cy, int r, uint16_t c) { ell(cx, cy, r, r, c); }
/* Punkt auf einem Kreis: a = 0..1023, 0 = rechts, 256 = unten */
static int ox(int cx, int a, int r) { return cx + icos(a) * r / 4096; }
static int oy(int cy, int a, int r) { return cy + isin(a) * r / 4096; }

/* Text mit schwarzem Kasten dahinter, damit er ueber dem Regen lesbar bleibt */
static void label(int x, int y, const char *t, uint16_t c) {
  if (!band_hit(y - 1, 7) || !*t)
    return;
  gfx_fill(x - 1, y - 1, gfx_text_w(t) + 2, 7, BG);
  gfx_text_plain(x, y, t, c);
}
static void cursor(int x, int y) {
  if (!band_hit(y - 1, 7))
    return;
  gfx_fill(x - 1, y - 1, 5, 7, BG);
  if ((esp_timer_get_time() / 400000) & 1)
    gfx_fill(x, y, 3, 5, HI);
}
/* Schreibmaschine: zeigt hoechstens *left Zeichen, Cursor am Ende */
static void typed(int x, int y, const char *t, int *left, uint16_t c) {
  int len = (int)strlen(t), n = *left < 0 ? 0 : *left < len ? *left : len;
  char b[32];
  snprintf(b, sizeof b, "%.*s", n, t);
  label(x, y, b, c);
  if (*left >= 0 && *left <= len)
    cursor(x + n * 4, y);
  *left -= len + 1;
}
static void prompt(int y, const char *t, int *left) {
  label(2, y, "> ", HI);
  typed(10, y, t, left, HI);
}

/* ------------------------------------------------------------ Matrix-Regen */

static const char RAIN_CHARS[] = "0123456789ABCDEF<>+=*#%";
static void draw_rain(int level) {
  for (int c = 0; c < RAIN_COLS; c++)
    for (int k = 0; k < rain[c].len; k++) {
      int row = rain[c].head - k, y = TOP + 1 + row * 6;
      if (row < 0 || row >= RAIN_ROWS || !band_hit(y, 5))
        continue;
      int g = (k ? 170 - k * 150 / rain[c].len : 255) * level / 256;
      uint16_t col = k ? RGB(0, g, g / 3) : RGB(g * 3 / 4, g, g * 3 / 4);
      uint32_t h = hash(c * 977u + row * 131u + (frame / (k ? 24 : 6)) * 7u);
      char s[2] = {RAIN_CHARS[h % (sizeof RAIN_CHARS - 1)], 0};
      gfx_text_plain(c * 4, y, s, col);
    }
}

/* ------------------------------------------------------------ Leisten */

/* kleines Wetter-Symbol (wie in der Eulen-Ansicht, einfarbig dunkel auf der Leiste) */
static void mini_weather(int x, int y, int code) {
  if (code <= 1) {
    gfx_fill(x + 1, y, 3, 5, BG);
    gfx_fill(x, y + 1, 5, 3, BG);
    return;
  }
  gfx_fill(x, y + 1, 6, 3, BG);
  gfx_fill(x + 1, y, 3, 1, BG);
  if (code >= 51) {
    gfx_px(x + 1, y + 4, BG);
    gfx_px(x + 4, y + 4, BG);
  }
}

static void topbar(bool paused) {
  if (!band_hit(0, 7))
    return;
  gfx_fill(0, 0, GW, 7, HI);
  bool city = config_tty_header();
  char ip[32];
  bool on = net_connected();
  if (city)
    text_simplify(ip, 14, sources_city()[0] ? sources_city() : "...");
  else if (on)
    net_address(ip, sizeof ip);
  else
    snprintf(ip, sizeof ip, "%s",
             net_setup_active() ? "AP 192.168.4.1" : "KEIN WLAN");
  gfx_text_plain(2, 1, ip, BG);
  int x = GW - 2;
  if (city && wx.valid) {   /* ganz rechts: Temperatur, davor das Symbol */
    char tb[12];
    snprintf(tb, sizeof tb, "%.0fC", wx.temp);
    x -= gfx_text_w(tb);
    gfx_text_plain(x, 1, tb, BG);
    x -= 8;
    mini_weather(x, 1, wx.code);
    x -= 3;
  }
  char t[8] = "--:--";
  if (net_time_valid()) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(t, sizeof t, "%H:%M", &tm);
    if ((esp_timer_get_time() / 500000) & 1)
      t[2] = ' ';   /* blinkender Doppelpunkt */
  }
  x -= gfx_text_w(t);
  gfx_text_plain(x, 1, t, BG);
  int sx = x;   /* linke Kante des Rechtsblocks */
  if (!city) {
    const char *st = paused ? "[||]" : on ? "[ON]" : "[OFF]";
    sx = x - 4 - gfx_text_w(st);
    gfx_text_plain(sx, 1, st, BG);
  }
  /* Update-Hinweis: 5x6 Download-Symbol links daneben (1 s an, 0,5 s aus), wenn Platz ist */
  int ax = sx - 6;
  if (ota_update_available(NULL, 0) && ax > 2 + gfx_text_w(ip) + 1 &&
      esp_timer_get_time() % 1500000 < 1000000) {
    static const char *const DL[6] = {"..X..", "..X..", "X.X.X", ".XXX.", "..X..", "XXXXX"};
    for (int r = 0; r < 6; r++)
      for (int c = 0; c < 5; c++)
        if (DL[r][c] == 'X')
          gfx_px(ax + c, 1 + r, BG);
  }
}
/* Unten nur noch: wie lange die Karte noch steht */
static void progress(unsigned ticks, unsigned duration) {
  if (!band_hit(GH - 2, 2))
    return;
  int w = duration ? (int)(ticks * GW / duration) : 0;
  if (w > GW)
    w = GW;
  gfx_fill(0, GH - 2, w, 2, DIM);
  gfx_px(w, GH - 2, HI);
  gfx_px(w, GH - 1, HI);
}

/* ------------------------------------------------------------ Wetter */

static void sun(int cx, int cy, int r) {
  for (int k = 0; k < 8; k++) {
    int a = frame * 3 + k * 128, len = r + 3 + ((k + frame / 10) & 1);
    gfx_line(ox(cx, a, r + 2), oy(cy, a, r + 2), ox(cx, a, len),
             oy(cy, a, len), AMBER);
  }
  disc(cx, cy, r, AMBER);
  disc(cx - r / 3, cy - r / 3, r / 3, RGB(255, 236, 150));
}
static void stars(int cx, int cy, int spread, int n) {
  for (int i = 0; i < n; i++) {
    uint32_t h = hash(i * 53u + 11u);
    int x = cx - spread + (int)(h % (2 * spread + 1)),
        y = cy - spread + (int)((h >> 8) % (2 * spread + 1));
    bool on = ((frame / 6 + h) % 7) == 0;
    gfx_px(x, y, on ? WHITE : DIM);
    if (on) {
      gfx_px(x - 1, y, DIM);
      gfx_px(x + 1, y, DIM);
      gfx_px(x, y - 1, DIM);
      gfx_px(x, y + 1, DIM);
    }
  }
}
static void moon(int cx, int cy, int r, int lit_pct) {
  disc(cx, cy, r, RGB(215, 240, 205));
  int off = lit_pct * (2 * r + 1) / 100;
  if (off < 2 * r + 1)
    disc(cx - off, cy - 1, r, BG);
}
static void cloud(int cx, int cy, uint16_t c) {
  disc(cx - 5, cy + 1, 4, c);
  disc(cx, cy - 2, 5, c);
  disc(cx + 5, cy + 1, 4, c);
  gfx_fill(cx - 8, cy + 1, 17, 5, c);
}
static int weather_class(int c) {
  if (c <= 1)
    return 0;
  if (c <= 3)
    return 1;
  if (c == 45 || c == 48)
    return 2;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86)
    return 4;
  if (c >= 95)
    return 5;
  return c >= 51 ? 3 : 1;
}
static void weather_icon(int cx, int cy, int code, bool day) {
  int cls = weather_class(code), dx = isin(frame * 4) * 2 / 4096;
  if (!day)
    stars(cx, cy - 4, 14, 9);
  if (cls == 0) {
    if (day)
      sun(cx, cy, 7);
    else
      moon(cx, cy, 7, 30);
    return;
  }
  if (cls == 1) {
    if (day)
      sun(cx - 5, cy - 5, 5);
    else
      moon(cx - 5, cy - 5, 5, 30);
    cloud(cx + 3 + dx, cy + 3, RGB(120, 170, 140));
    return;
  }
  bool flash = cls == 5 && frame % 100 < 6;
  cloud(cx + dx, cy - 4, flash ? WHITE : RGB(90, 135, 110));
  if (cls == 2)
    for (int row = 0; row < 3; row++)
      for (int x = -11; x <= 11; x++)
        if ((x + 40 + (int)frame / 2 + row * 5) % 8 < 5)
          gfx_px(cx + x, cy + 6 + row * 3, row == 1 ? FG : DIM);
  if (cls == 3 || cls == 5)
    for (int i = 0; i < 7; i++) {
      int x = cx - 9 + i * 3,
          y = cy + 3 + (int)((frame * 3 / 2 + hash(i) % 16) % 14);
      gfx_fill(x, y, 1, 2, CYAN);
    }
  if (cls == 4)
    for (int i = 0; i < 7; i++) {
      int x = cx - 9 + i * 3 + isin(frame * 8 + i * 140) * 2 / 4096,
          y = cy + 3 + (int)((frame / 3 + hash(i) % 14) % 14);
      gfx_px(x, y, WHITE);
    }
  if (flash) {
    gfx_line(cx + 1, cy + 1, cx - 2, cy + 6, AMBER);
    gfx_line(cx - 2, cy + 6, cx + 2, cy + 6, AMBER);
    gfx_line(cx + 2, cy + 6, cx - 1, cy + 12, AMBER);
  }
}
static uint16_t heat(int t) {
  /* 0 Grad kalt-blau bis 25 Grad warm-orange */
  int k = t * 255 / 25;
  if (k < 0)
    k = 0;
  if (k > 255)
    k = 255;
  return RGB(95 + k * 160 / 255, 215 - k * 60 / 255, 255 - k * 230 / 255);
}
static void forecast(int y, const char *name, float lo, float hi,
                     unsigned ticks) {
  label(3, y, name, DIM);
  int x0 = 30, x1 = 78;
  for (int x = x0; x <= x1; x += 2)
    gfx_px(x, y + 2, DARK);
  int a = x0 + ((int)lo + 10) * (x1 - x0) / 45,
      b = x0 + ((int)hi + 10) * (x1 - x0) / 45;
  if (a < x0)
    a = x0;
  if (b > x1)
    b = x1;
  int grow = ticks < 20 ? (b - a) * (int)ticks / 20 : b - a;
  for (int x = a; x <= a + grow; x++)
    gfx_fill(x, y + 1, 1, 3, heat(-10 + (x - x0) * 45 / (x1 - x0)));
  char s[16];
  snprintf(s, sizeof s, "%.0f/%.0f", lo, hi);
  label(82, y, s, FG);
}
static void weather_card(const card_t *card, unsigned ticks) {
  int left = 9999;
  prompt(9, card->head, &left);
  if (!wx.valid) {
    label(3, 22, "KEINE WETTERDATEN", DIM);
    return;
  }
  if (band_hit(16, 34))
    gfx_fill(1, 16, 39, 34, BG);
  weather_icon(20, 32, wx.code, wx.is_day);
  char b[24];
  snprintf(b, sizeof b, "%.1f", wx.temp);
  int w = gfx_text_w(b) * 2 + 2;
  if (band_hit(16, 14))
    gfx_fill(41, 16, w + 12, 13, BG);
  gfx_text_big(43, 18, b, WHITE, 2);
  gfx_fill(43 + w + 1, 18, 2, 2, WHITE);
  gfx_text_plain(43 + w + 4, 18, "C", FG);
  snprintf(b, sizeof b, "WIND %.0f KMH", wx.wind);
  label(43, 33, b, FG);
  snprintf(b, sizeof b, "REGEN %d%%", wx.rain[0]);
  label(43, 40, b, wx.rain[0] >= 50 ? CYAN : FG);
  forecast(53, "HEUTE", wx.tmin[0], wx.tmax[0], ticks);
  forecast(62, "MORGEN", wx.tmin[1], wx.tmax[1], ticks);
}

/* ------------------------------------------------------------ Fakten */

static void clock_face(int cx, int cy) {
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  int ms = (int)(esp_timer_get_time() / 1000 % 1000);
  for (int h = 0; h < 12; h++)
    gfx_px(ox(cx, h * 1024 / 12, 10), oy(cy, h * 1024 / 12, 10),
           h % 3 ? DIM : FG);
  int sa = ((tm.tm_sec * 1000 + ms) * 1024 / 60000) - 256,
      ma = (tm.tm_min * 60 + tm.tm_sec) * 1024 / 3600 - 256,
      ha = ((tm.tm_hour % 12) * 60 + tm.tm_min) * 1024 / 720 - 256;
  gfx_line(cx, cy, ox(cx, ha, 5), oy(cy, ha, 5), FG);
  gfx_line(cx, cy, ox(cx, ma, 8), oy(cy, ma, 8), FG);
  gfx_line(cx, cy, ox(cx, sa, 9), oy(cy, sa, 9), AMBER);
  gfx_px(cx, cy, WHITE);
}
static void icon(int which, int cx, int cy, const char *big) {
  switch (which) {
  case ICON_SUN:
    sun(cx, cy, 5);
    break;
  case ICON_MOON:
    stars(cx, cy, 11, 7);
    moon(cx, cy, 8, atoi(big));
    break;
  case ICON_LEAF: {
    int s = isin(frame * 6) * 3 / 4096;
    gfx_line(cx, cy + 9, cx + s, cy + 3, DIM);
    for (int dy = -7; dy <= 3; dy++) {
      int h = isq(49 - (dy + 2) * (dy + 2) > 0 ? 49 - (dy + 2) * (dy + 2) : 0) / 2;
      int x = cx + s * (3 - dy) / 10;
      gfx_fill(x - h, cy + dy, 2 * h + 1, 1, dy < -2 ? HI : RGB(40, 200, 100));
      gfx_px(x, cy + dy, DARK);
    }
    break;
  }
  case ICON_AIR:
    for (int i = 0; i < 4; i++) {
      int y = cy - 7 + i * 5,
          x = cx - 14 + (int)((frame * (1 + i % 2) + i * 13) % 30);
      gfx_line(x, y, x + 7, y, i & 1 ? CYAN : FG);
      gfx_px(x + 8, y - 1, i & 1 ? CYAN : FG);
    }
    break;
  case ICON_QUAKE: {
    bool burst = (frame / 30) % 4 == 0;
    gfx_line(cx - 11, cy, cx + 11, cy, DARK);
    int py = cy;
    for (int x = -11; x <= 11; x++) {
      int amp = burst ? 7 - (x < 0 ? -x : x) / 3 : 2;
      int y = cy + isin(x * 90 + frame * 60) * amp / 4096 +
              (burst ? (int)(hash(x + frame * 31) % 3) - 1 : 0);
      if (x > -11)
        gfx_line(cx + x - 1, py, cx + x, y, burst ? AMBER : HI);
      py = y;
    }
    break;
  }
  case ICON_PARTY: {
    const uint16_t pal[] = {AMBER, CYAN, HI, PINK, WHITE};
    for (int i = 0; i < 16; i++) {
      int x = cx - 10 + (int)(hash(i) % 21) + isin(frame * 5 + i * 70) * 2 / 4096,
          y = cy - 10 + (int)((frame / 2 + hash(i * 7) % 21) % 21);
      gfx_fill(x, y, (frame / 4 + i) & 1 ? 2 : 1, (frame / 4 + i) & 1 ? 1 : 2,
               pal[i % 5]);
    }
    break;
  }
  case ICON_CAL: {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    gfx_fill(cx - 9, cy - 8, 19, 18, DARK);
    gfx_fill(cx - 9, cy - 8, 19, 5, HI);
    gfx_px(cx - 5, cy - 9, FG);
    gfx_px(cx + 5, cy - 9, FG);
    char d[12];
    snprintf(d, sizeof d, "%d", tm.tm_mday);
    gfx_text_big(cx - gfx_text_w(d), cy - 1, d, WHITE, 2);
    break;
  }
  case ICON_ROCKET: {
    for (int i = 0; i < 9; i++) {
      int x = cx - 11 + (int)(hash(i) % 23),
          y = cy - 11 + (int)((frame / (1 + i % 3) + hash(i * 3)) % 23);
      gfx_px(x, y, i % 3 ? DIM : FG);
    }
    int d = isin(frame * 8) / 4096;
    gfx_fill(cx - 2, cy - 6 + d, 5, 9, WHITE);
    gfx_fill(cx - 1, cy - 8 + d, 3, 2, WHITE);
    gfx_px(cx, cy - 9 + d, WHITE);
    gfx_px(cx, cy - 3 + d, CYAN);
    gfx_fill(cx - 4, cy + 1 + d, 2, 3, PINK);
    gfx_fill(cx + 3, cy + 1 + d, 2, 3, PINK);
    gfx_fill(cx - 1, cy + 3 + d, 3, 2 + (frame / 2) % 3, AMBER);
    gfx_px(cx, cy + 3 + d, WHITE);
    break;
  }
  case ICON_COIN: {
    int w = icos(frame * 12);
    w = (w < 0 ? -w : w) * 8 / 4096;
    ell(cx, cy, w + 1, 8, RGB(170, 115, 20));
    if (w > 1)
      ell(cx, cy, w - 1, 6, AMBER);
    if (w > 3) /* Glanzlicht wandert mit der Drehung */
      gfx_fill(cx - w / 2, cy - 4, 1, 6, RGB(255, 236, 150));
    break;
  }
  }
}
static void meter(int y, const char *name, int pct, unsigned ticks) {
  if (ticks < 30)
    pct = pct * (int)ticks / 30;
  label(3, y, name, DIM);
  int x0 = 26, x1 = 84, fill = x0 + (x1 - x0) * pct / 100;
  for (int x = x0; x <= x1; x += 2)
    gfx_fill(x, y + 1, 1, 3, DARK);
  for (int x = x0; x < fill; x += 2)
    gfx_fill(x, y + 1, 1, 3, x + 2 >= fill ? WHITE : HI);
  char s[16];
  snprintf(s, sizeof s, "%d%%", pct);
  label(88, y, s, FG);
}
static void fact_card(const card_t *card, const char lines[][26], int n,
                      unsigned ticks) {
  int left = 9999;
  prompt(9, card->head, &left);
  bool clock = card->src == SRC_CLOCK && net_time_valid();
  char big[16];
  snprintf(big, sizeof big, "%s", card->big);
  if (clock) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(big, sizeof big, "%H:%M", &tm);
  }
  /* Zahl "entschluesselt" sich von links nach rechts */
  int len = (int)strlen(big);
  for (int i = 0; i < len; i++)
    if (big[i] != ' ' && ticks < 8 + (unsigned)i * 4)
      big[i] = "0123456789#%*"[hash(i * 17u + frame / 2) % 13];
  int k = len * 12 <= 76 ? 3 : 2;
  if (band_hit(18, k * 5 + 3))
    gfx_fill(2, 18, len * 4 * k + 2, k * 5 + 3, BG);
  gfx_text_big(3, 19, big, HI, k);
  if (clock) {
    char s[8];
    snprintf(s, sizeof s, ":%02d", (int)(time(NULL) % 60));
    label(3 + len * 4 * k, 19 + k * 5 - 5, s, FG);
  }
  if (band_hit(14, 25))
    gfx_fill(80, 14, 26, 25, BG);
  if (clock)
    clock_face(92, 26);
  else
    icon(card->icon, 92, 26, card->big);
  int body = card->big[0] ? 1 : 0, t = (int)ticks * 2;
  for (int i = 0; i < 3 && body + i < n; i++)
    typed(3, 43 + i * 7, lines[body + i], &t, FG);
  /* Balken unten: Mond beleuchtet / wie weit der Tag ist */
  int pct = -1;
  const char *what = "";
  if (clock) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    pct = (tm.tm_hour * 60 + tm.tm_min) * 100 / 1440;
    what = "TAG";
  } else if (card->icon == ICON_MOON && card->src == SRC_MOON) {
    pct = atoi(card->big);
    what = "LICHT";
  }
  if (pct >= 0)
    meter(64, what, pct, ticks);
}

/* ------------------------------------------------------------ Text & Boot */

/* Wie ein Terminal: tippt Zeile fuer Zeile; ist der Bildschirm voll, gleitet
 * alles nach oben und die oberste Zeile verschwindet. */
static void text_card(const card_t *card, const char lines[][26], int n,
                      unsigned ticks, bool paused) {
  int left = paused ? 99999 : (int)(ticks * TTY_CPS / 30);
  char head[26];
  snprintf(head, sizeof head, "%.23s", card->head);
  prompt(9, head, &left);
  /* welche Zeile wird gerade getippt, wie weit ist sie? */
  int line = 0, into = 0, budget = left;
  for (; line < n; line++) {
    int len = (int)strlen(lines[line]) + 1;
    if (budget < len) {
      into = budget < 0 ? 0 : budget;
      break;
    }
    budget -= len;
  }
  if (line >= n) {
    line = n;
    into = 7;
  }
  /* Scroll in Pixeln: neue Zeile unterhalb des Fensters schiebt in 4 Zeichen hoch */
  int off = 0;
  if (line >= TTY_ROWS) {
    int step = into * 7 / 4;
    off = (line - TTY_ROWS) * 7 + (step > 7 ? 7 : step);
  }
  int first = off / 7;
  for (int i = first; i < n && i <= first + TTY_ROWS; i++) {
    int y = 17 + i * 7 - off, before = left;
    for (int k = 0; k < i; k++)
      before -= (int)strlen(lines[k]) + 1;
    if (y < 16 || y + 5 > BOTTOM)
      continue;
    typed(3, y, lines[i], &before, FG);
  }
  if (line >= n && !paused) {
    int y = 17 + n * 7 - off;
    if (y >= 16 && y + 5 <= BOTTOM)
      cursor(3, y);
  }
}
static void boot(unsigned ticks) {
  int left = (int)ticks * 2;
  char ip[32], b[40];
  net_address(ip, sizeof ip);
  prompt(11, "BOOT NEWSBOARD", &left);
  typed(3, 20, "[ OK ] DISPLAY 320X240", &left, FG);
  snprintf(b, sizeof b, net_connected() ? "[ OK ] WLAN %s" : "[ .. ] WLAN SUCHT",
           ip);
  typed(3, 27, b, &left, net_connected() ? FG : DIM);
  typed(3, 34, net_time_valid() ? "[ OK ] ZEIT SYNC" : "[ .. ] WARTE AUF NTP",
        &left, net_time_valid() ? FG : DIM);
  prompt(44, "FETCH DATEN ...", &left);
  if (left > 0)
    cursor(10 + 16 * 4, 44);
}

/* ------------------------------------------------------------ Push-Nachricht */

static void alert_bar(const notify_t *nt, unsigned ticks) {
  if (!band_hit(0, 7))
    return;
  bool flash = ticks < 90 && ((ticks / 8) & 1);
  gfx_fill(0, 0, GW, 7, flash ? HI : AMBER);
  gfx_text_plain(2, 1, "!! NEUE NACHRICHT", BG);
  char t[8] = "";
  if (net_time_valid()) {
    struct tm tm;
    localtime_r(&nt->at, &tm);
    strftime(t, sizeof t, "%H:%M", &tm);
  }
  gfx_text_plain(GW - 2 - gfx_text_w(t), 1, t, BG);
}
/* 9x8 Pixel ab (x, y), je nach Art der Nachricht animiert */
static void notify_icon(int kind, int x, int y) {
  if (!band_hit(y - 3, 12))
    return;
  gfx_fill(x - 2, y - 3, 13, 12, BG);
  switch (kind) {
  case NK_CHAT: /* Sprechblase, drei Punkte "tippen" */
    gfx_fill(x, y, 9, 6, CYAN);
    gfx_px(x, y, BG);
    gfx_px(x + 8, y, BG);
    gfx_px(x, y + 5, BG);
    gfx_px(x + 8, y + 5, BG);
    gfx_px(x + 2, y + 6, CYAN);
    gfx_px(x + 1, y + 7, CYAN);
    for (int i = 0; i < 3; i++)
      gfx_px(x + 2 + i * 2, y + 2 + ((frame / 6) % 3 == (unsigned)i ? 0 : 1),
             (frame / 6) % 3 == (unsigned)i ? WHITE : BG);
    break;
  case NK_MAIL: { /* Brief, Klappe geht auf und zu */
    gfx_fill(x, y + 1, 9, 7, WHITE);
    gfx_fill(x + 1, y + 2, 7, 5, DARK);
    bool open = (frame / 20) & 1;
    int ty = open ? y - 2 : y + 4;
    gfx_line(x, y + 1, x + 4, ty, WHITE);
    gfx_line(x + 4, ty, x + 8, y + 1, WHITE);
    if (open)
      gfx_fill(x + 2, y - 1, 5, 2, AMBER);
    break;
  }
  case NK_CALL: { /* Hoerer vibriert, Wellen blinken */
    int dx = frame % 30 < 15 ? ((frame / 2) & 1 ? 1 : -1) : 0;
    gfx_fill(x + dx, y, 3, 2, HI);
    gfx_fill(x + dx, y + 2, 1, 4, HI);
    gfx_fill(x + dx, y + 6, 3, 2, HI);
    if ((frame / 8) & 1) {
      gfx_px(x + 5, y + 2, FG);
      gfx_px(x + 6, y + 3, FG);
      gfx_px(x + 6, y + 4, FG);
      gfx_px(x + 5, y + 5, FG);
    }
    if ((frame / 8) % 3 == 2) {
      gfx_px(x + 7, y + 1, DIM);
      gfx_px(x + 8, y + 3, DIM);
      gfx_px(x + 8, y + 4, DIM);
      gfx_px(x + 7, y + 6, DIM);
    }
    break;
  }
  default: { /* Glocke schwingt */
    int s = isin(frame * 16) * 2 / 4096;
    gfx_fill(x + 3 + s, y, 3, 1, AMBER);
    gfx_fill(x + 2 + s, y + 1, 5, 4, AMBER);
    gfx_fill(x + 1 + s, y + 5, 7, 1, AMBER);
    gfx_px(x + 4 - s, y + 6, WHITE);
    break;
  }
  }
}
void tty_draw_notify(const card_t *card, const char lines[][26], int n,
                     unsigned ticks, unsigned duration, const notify_t *nt) {
  gfx_fill(0, 0, GW, GH, BG);
  draw_rain(140);
  alert_bar(nt, ticks);
  text_card(card, lines, n, ticks, false);
  notify_icon(nt->kind, 95, 9);
  progress(ticks, duration);
}

void tty_draw(const card_t *card, const char lines[][26], int n,
              unsigned page, unsigned ticks, unsigned duration, bool paused) {
  gfx_fill(0, 0, GW, GH, BG);
  if (!card)
    draw_rain(230);
  else if (card->kind == CARD_TEXT)
    draw_rain(110);
  else
    draw_rain(card->src == SRC_CLOCK ? 200 : 90);
  topbar(paused);
  if (!card)
    boot(ticks);
  else if (card->kind == CARD_WEATHER)
    weather_card(card, ticks);
  else if (card->kind == CARD_FACT)
    fact_card(card, lines, n, ticks);
  else
    text_card(card, lines, n, ticks, paused);
  progress(card ? ticks : 0, duration);
}
