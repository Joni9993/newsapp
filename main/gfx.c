#include "gfx.h"
#include "lcd.h"
#include <string.h>
#include <math.h>

static uint16_t band[GW * GFX_BAND];
static uint16_t out[2][LCD_W * GFX_BAND * GSCALE];
static int cur, by0;
static int16_t sintab[1024];
static uint16_t outline = RGB(14, 8, 16);

void gfx_outline_color(uint16_t c) { outline = c; }

int isin(int a) { return sintab[a & 1023]; }
int icos(int a) { return sintab[(a + 256) & 1023]; }

static void tables(void)
{
    static bool ok;
    if (ok) return;
    for (int i = 0; i < 1024; i++) sintab[i] = (int16_t)lroundf(sinf(i * 6.2831853f / 1024) * 4096);
    ok = true;
}

void gfx_band_begin(int y0) { tables(); by0 = y0; }
int  gfx_band_y0(void) { return by0; }

void gfx_band_end(void)
{
    /* Puffer cur war vor zwei Streifen unterwegs - erst warten, bis er frei ist. */
    lcd_wait(1);
    uint16_t *o = out[cur];
    for (int y = 0; y < GFX_BAND; y++) {
        uint16_t *row = o + y * GSCALE * LCD_W;
        const uint16_t *src = band + y * GW;
        for (int px = 0, x = 0, k = 0; px < LCD_W; px++) {
            uint16_t c = src[x];
            row[px] = (uint16_t)((c >> 8) | (c << 8));   /* SPI will das hohe Byte zuerst */
            if (++k == GSCALE) { k = 0; x++; }
        }
        for (int r = 1; r < GSCALE; r++) memcpy(row + r * LCD_W, row, LCD_W * 2);
    }
    lcd_push(o, GFX_BAND * GSCALE);
    cur ^= 1;
}

static inline bool in_band(int y) { return (unsigned)(y - by0) < GFX_BAND; }

void gfx_px(int x, int y, uint16_t c)
{
    if ((unsigned)x < GW && in_band(y)) band[(y - by0) * GW + x] = c;
}

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
    int y0 = y > by0 ? y : by0, y1 = y + h < by0 + GFX_BAND ? y + h : by0 + GFX_BAND;
    int x0 = x > 0 ? x : 0, x1 = x + w < GW ? x + w : GW;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) band[(yy - by0) * GW + xx] = c;
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_circle(int cx, int cy, int r, uint16_t c)
{
    if (cy + r < by0 || cy - r >= by0 + GFX_BAND) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_px(cx + x, cy + y, c); gfx_px(cx - x, cy + y, c);
        gfx_px(cx + x, cy - y, c); gfx_px(cx - x, cy - y, c);
        gfx_px(cx + y, cy + x, c); gfx_px(cx - y, cy + x, c);
        gfx_px(cx + y, cy - x, c); gfx_px(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

static inline uint16_t fx(uint16_t c, int flags, int x, int y)
{
    if (flags & SPR_SHADOW) return outline;
    if (flags & SPR_WHITE) return 0xFFFF;
    if (flags & SPR_RED) {
        int r = (c >> 11) + 12, g = ((c >> 5) & 63) >> 1, b = (c & 31) >> 1;
        return (uint16_t)(((r > 31 ? 31 : r) << 11) | (g << 5) | b);
    }
    if (flags & SPR_DARK) return (uint16_t)((c >> 1) & 0x7BEF);
    if (flags & SPR_ICE) {
        int r = (c >> 11) >> 1, gg = ((c >> 5) & 63) * 3 / 4 + 10, b = (c & 31) + 12;
        return (uint16_t)((r << 11) | ((gg > 63 ? 63 : gg) << 5) | (b > 31 ? 31 : b));
    }
    if (flags & SPR_VIOLET) {
        int r = ((c >> 11) >> 1) + 8, gg = ((c >> 5) & 63) / 4, b = ((c & 31) >> 1) + 14;
        return (uint16_t)(((r > 31 ? 31 : r) << 11) | (gg << 5) | (b > 31 ? 31 : b));
    }
    return c;
}

void gfx_spr(const spr_t *s, int x, int y, int flags)
{
    if (!s || y + s->h <= by0 || y >= by0 + GFX_BAND) return;
    int sy0 = by0 - y > 0 ? by0 - y : 0;
    int sy1 = by0 + GFX_BAND - y < s->h ? by0 + GFX_BAND - y : s->h;
    for (int sy = sy0; sy < sy1; sy++) {
        uint16_t *d = band + (y + sy - by0) * GW;
        const uint16_t *p = s->px + sy * s->w;
        for (int sx = 0; sx < s->w; sx++) {
            int dx = x + ((flags & SPR_FLIP) ? s->w - 1 - sx : sx);
            if ((unsigned)dx >= GW) continue;
            uint16_t c = p[sx];
            if (c == SPR_KEY) continue;
            if ((flags & SPR_GHOST) && ((dx + y + sy) & 1)) continue;
            d[dx] = fx(c, flags, dx, y + sy);
        }
    }
}

void gfx_spr_rot_s(const spr_t *s, int px, int py, int ox, int oy, int ang, int scale, int flags)
{
    if (!s || scale <= 0) return;
    /* Huelle: die vier Ecken vorwaerts drehen und skalieren, nur dieses Rechteck ablaufen */
    int fc = icos(ang) * scale / 256, fs = isin(ang) * scale / 256;
    int xmin = 9999, xmax = -9999, ymin = 9999, ymax = -9999;
    for (int k = 0; k < 4; k++) {
        int cx = (k & 1 ? s->w : 0) - ox, cy = (k & 2 ? s->h : 0) - oy;
        int qx = (cx * fc - cy * fs) >> 12, qy = (cx * fs + cy * fc) >> 12;
        if (qx < xmin) xmin = qx;
        if (qx > xmax) xmax = qx;
        if (qy < ymin) ymin = qy;
        if (qy > ymax) ymax = qy;
    }
    xmin -= 1; ymin -= 1; xmax += 2; ymax += 2;
    if (py + ymax < by0 || py + ymin >= by0 + GFX_BAND) return;
    /* Drehung und Groesse in einem: cos/sin werden durch den Massstab geteilt */
    int cs = icos(ang) * 256 / scale, sn = isin(ang) * 256 / scale;
    int y0 = py + ymin > by0 ? py + ymin : by0, y1 = py + ymax < by0 + GFX_BAND ? py + ymax : by0 + GFX_BAND;
    for (int y = y0; y < y1; y++) {
        int dy = y - py;
        for (int x = px + xmin; x < px + xmax; x++) {
            if ((unsigned)x >= GW) continue;
            int dx = x - px;
            /* Rueckwaerts drehen: wo im Sprite liegt dieses Pixel? */
            int sx = ((dx * cs + dy * sn) >> 12) + ox;
            int sy = ((-dx * sn + dy * cs) >> 12) + oy;
            if ((unsigned)sx >= s->w || (unsigned)sy >= s->h) continue;
            if (flags & SPR_FLIP) sx = s->w - 1 - sx;
            uint16_t c = s->px[sy * s->w + sx];
            if (c == SPR_KEY) continue;
            if ((flags & SPR_GHOST) && ((x + y) & 1)) continue;
            band[(y - by0) * GW + x] = fx(c, flags, x, y);
        }
    }
}

void gfx_spr_rot(const spr_t *s, int px, int py, int ox, int oy, int ang, int flags)
{
    gfx_spr_rot_s(s, px, py, ox, oy, ang, 256, flags);
}

/* Vergroessert ohne Drehung: nur das Rechteck des Sprites wird abgelaufen, nicht ein Quadrat
 * um den Drehpunkt - fuer grosse Figuren um ein Vielfaches schneller. */
void gfx_spr_scaled(const spr_t *s, int x0, int y0, int scale, int flags)
{
    if (!s) return;
    if (scale == 256) { gfx_spr(s, x0, y0, flags); return; }
    int dw = s->w * scale / 256, dh = s->h * scale / 256;
    if (y0 + dh <= by0 || y0 >= by0 + GFX_BAND) return;
    int step = 65536 * 256 / scale;   /* Quellpixel je Zielpixel, 16.16 */
    int ya = y0 > by0 ? y0 : by0, yb = y0 + dh < by0 + GFX_BAND ? y0 + dh : by0 + GFX_BAND;
    for (int y = ya; y < yb; y++) {
        int sy = (int)(((int64_t)(y - y0) * step) >> 16);
        if (sy >= s->h) continue;
        const uint16_t *row = s->px + sy * s->w;
        uint16_t *d = band + (y - by0) * GW;
        for (int dx = 0, acc = 0; dx < dw; dx++, acc += step) {
            int x = x0 + dx;
            if ((unsigned)x >= GW) continue;
            int sx = acc >> 16;
            if (sx >= s->w) break;
            if (flags & SPR_FLIP) sx = s->w - 1 - sx;
            uint16_t c = row[sx];
            if (c == SPR_KEY) continue;
            if ((flags & SPR_GHOST) && ((x + y) & 1)) continue;
            d[x] = fx(c, flags, x, y);
        }
    }
}

void gfx_spr_scaled_outline(const spr_t *s, int x0, int y0, int scale, int flags)
{
    int f = (flags & (SPR_FLIP | SPR_GHOST)) | SPR_SHADOW;
    gfx_spr_scaled(s, x0 - 1, y0, scale, f); gfx_spr_scaled(s, x0 + 1, y0, scale, f);
    gfx_spr_scaled(s, x0, y0 - 1, scale, f); gfx_spr_scaled(s, x0, y0 + 1, scale, f);
    gfx_spr_scaled(s, x0, y0, scale, flags);
}

void gfx_spr_outline(const spr_t *s, int x, int y, int flags)
{
    int f = (flags & (SPR_FLIP | SPR_GHOST)) | SPR_SHADOW;
    gfx_spr(s, x - 1, y, f); gfx_spr(s, x + 1, y, f);
    gfx_spr(s, x, y - 1, f); gfx_spr(s, x, y + 1, f);
    gfx_spr(s, x, y, flags);
}

void gfx_spr_rot_outline_s(const spr_t *s, int px, int py, int ox, int oy, int ang, int scale, int flags)
{
    int f = (flags & (SPR_FLIP | SPR_GHOST)) | SPR_SHADOW;
    gfx_spr_rot_s(s, px - 1, py, ox, oy, ang, scale, f); gfx_spr_rot_s(s, px + 1, py, ox, oy, ang, scale, f);
    gfx_spr_rot_s(s, px, py - 1, ox, oy, ang, scale, f); gfx_spr_rot_s(s, px, py + 1, ox, oy, ang, scale, f);
    gfx_spr_rot_s(s, px, py, ox, oy, ang, scale, flags);
}

void gfx_spr_rot_outline(const spr_t *s, int px, int py, int ox, int oy, int ang, int flags)
{
    gfx_spr_rot_outline_s(s, px, py, ox, oy, ang, 256, flags);
}

/* 3x5-Schrift: 15 Bit, oberste Zeile in den hohen Bits. */
static uint16_t glyph(char ch)
{
    /* 1..3 = Ae Oe Ue (Grundbuchstabe, Punkte zeichnet accent()), 4 = Eszett */
    if (ch == 1) ch = 'A'; else if (ch == 2) ch = 'O'; else if (ch == 3) ch = 'U'; else if (ch == 4) return 0x3BAE;
    static const uint16_t dig[10] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7292, 0x7BEF, 0x7BCF };
    static const uint16_t abc[26] = {
        0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A,
        0x5BAD, 0x4927, 0x5FED, 0x6B6D, 0x2B6A, 0x6BA4, 0x2B73, 0x6BAD, 0x388E, 0x7492,
        0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7 };
    if (ch >= '0' && ch <= '9') return dig[ch - '0'];
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return abc[ch - 'A'];
    switch (ch) {
    case '+': return 0x05D0; case '-': return 0x01C0; case '!': return 0x2482;
    case ':': return 0x0410; case '.': return 0x0002; case '/': return 0x12A4;
    case '%': return 0x52A5; case '*': return 0x0AA8;
    case '(': return 0x2922; case ')': return 0x224A; case '"': return 0x5A00;
    case ',': return 0x0014; case '?': return 0x7282; case '=': return 0x0E38;
    case '#': return 0x5F7D; case '@': return 0x7BE3; case '<': return 0x1511;
    case '>': return 0x4454; case '\'': return 0x2400;
    case '_': return 0x0007; case '[': return 0x6926; case ']': return 0x324B;
    case '|': return 0x2492;
    }
    return 0;
}

/* Umlautpunkte: eine Zeile ueber der Schrift (in der Zeilenluecke) */
static bool accent(char ch) { return ch >= 1 && ch <= 3; }

int gfx_text_w(const char *t) { int n = (int)strlen(t); return n ? n * 4 - 1 : 0; }

static void text1(int x, int y, const char *t, uint16_t c)
{
    for (; *t; t++, x += 4) {
        uint16_t g = glyph(*t);
        for (int i = 0; i < 15; i++)
            if (g & (0x4000 >> i)) gfx_px(x + i % 3, y + i / 3, c);
        if (accent(*t)) { gfx_px(x, y - 1, c); gfx_px(x + 2, y - 1, c); }
    }
}

/* Vergroesserte Schrift: jedes Schriftpixel wird ein Quadrat der Kantenlaenge k, mit Schatten */
void gfx_text_big(int x, int y, const char *t, uint16_t c, int k)
{
    if (y + 6 * k < by0 || y - k >= by0 + GFX_BAND) return;
    for (int pass = 0; pass < 2; pass++) {
        int ox = pass ? 0 : 1, cx = x;
        for (const char *p = t; *p; p++, cx += 4 * k) {
            uint16_t g = glyph(*p);
            for (int i = 0; i < 15; i++)
                if (g & (0x4000 >> i)) gfx_fill(cx + (i % 3) * k + ox, y + (i / 3) * k + ox, k, k, pass ? c : RGB(10, 8, 16));
            if (accent(*p)) {
                uint16_t sc = pass ? c : RGB(10, 8, 16);
                gfx_fill(cx + ox, y - k + ox, k, k, sc);
                gfx_fill(cx + 2 * k + ox, y - k + ox, k, k, sc);
            }
        }
    }
}

/* Ohne Schatten - fuer dunkle Schrift auf hellem Grund */
void gfx_text_plain(int x, int y, const char *t, uint16_t c)
{
    if (y + 5 < by0 || y - 1 >= by0 + GFX_BAND) return;
    text1(x, y, t, c);
}

void gfx_text(int x, int y, const char *t, uint16_t c)
{
    if (y + 6 < by0 || y - 1 >= by0 + GFX_BAND) return;
    text1(x + 1, y + 1, t, RGB(16, 8, 16));
    text1(x, y, t, c);
}

#define VM 20   /* Breite des Vignetten-Rands */
void gfx_light(int ar, int ag, int ab, const light_t *l, int n, int vign, int redpulse)
{
    /* Nur Lichter, die den Streifen erreichen; je Zeile wird jedes Licht nur ueber die Spanne
     * addiert, die es wirklich trifft - statt fuer jeden Pixel jedes Licht zu pruefen. */
    light_t act[16];
    int inv[16], na = 0;
    for (int i = 0; i < n && na < 16; i++)
        if (l[i].r > 0 && l[i].y + l[i].r > by0 && l[i].y - l[i].r < by0 + GFX_BAND) {
            act[na] = l[i];
            inv[na++] = 65536 / (l[i].r * l[i].r);
        }
    static int16_t LR[GW], LG[GW], LB[GW], VX[GW];
    static bool vx_ok;
    if (!vx_ok) {
        for (int x = 0; x < GW; x++) VX[x] = (int16_t)(x < VM ? VM - x : (x > GW - VM - 1 ? x - (GW - VM - 1) : 0));
        vx_ok = true;
    }
    for (int yy = 0; yy < GFX_BAND; yy++) {
        int y = by0 + yy;
        int ey = y < VM ? VM - y : (y > GH - VM - 1 ? y - (GH - VM - 1) : 0);
        for (int x = 0; x < GW; x++) { LR[x] = (int16_t)ar; LG[x] = (int16_t)ag; LB[x] = (int16_t)ab; }
        for (int i = 0; i < na; i++) {
            const light_t *L = &act[i];
            int dy = y - L->y, r2 = L->r * L->r, rem = r2 - dy * dy;
            if (rem <= 0) continue;
            int span = 0;
            while ((span + 1) * (span + 1) < rem) span++;   /* halbe Breite der getroffenen Strecke */
            int x0 = L->x - span < 0 ? 0 : L->x - span, x1 = L->x + span >= GW ? GW - 1 : L->x + span;
            for (int x = x0; x <= x1; x++) {
                int dx = x - L->x, d2 = dx * dx + dy * dy;
                int f = 256 - (d2 * inv[i] >> 8);
                if (f <= 0) continue;
                LR[x] += (int16_t)(f * L->cr >> 8); LG[x] += (int16_t)(f * L->cg >> 8); LB[x] += (int16_t)(f * L->cb >> 8);
            }
        }
        uint16_t *row = band + yy * GW;
        for (int x = 0; x < GW; x++) {
            int lr = LR[x], lg = LG[x], lb = LB[x];
            /* Vignette: je naeher am Rand, desto dunkler */
            int e = VX[x] > ey ? VX[x] : ey;   /* 0..VM */
            if (e) {
                int ed = e * e * vign / (VM * VM);      /* 0..vign */
                lr = lr * (256 - ed) >> 8; lg = lg * (256 - ed) >> 8; lb = lb * (256 - ed) >> 8;
            }
            uint16_t c = row[x];
            int r = (c >> 11) * lr >> 8, g = ((c >> 5) & 63) * lg >> 8, b = (c & 31) * lb >> 8;
            if (redpulse && e) r += e * redpulse / (VM * 8);
            row[x] = (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
        }
    }
}
