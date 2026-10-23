#include "rss.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { M_TEXT, M_TAG, M_CDATA, M_COMMENT };

void rss_init(rss_t *r, rss_cb cb, void *ctx)
{
    memset(r, 0, sizeof *r);
    r->cb = cb;
    r->ctx = ctx;
}

static int put_utf8(char *o, unsigned c)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63)); o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63));
    return 4;
}

/* &amp; &lt; &#228; ... in place; Unbekanntes bleibt stehen */
static void decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s != '&') { *o++ = *s++; continue; }
        const char *e = strchr(s, ';');
        if (!e || e - s > 10) { *o++ = *s++; continue; }
        unsigned c = 0;
        if (s[1] == '#') {
            c = (s[2] == 'x' || s[2] == 'X') ? (unsigned)strtoul(s + 3, NULL, 16) : (unsigned)strtoul(s + 2, NULL, 10);
        } else if (!strncmp(s, "&amp;", 5)) c = '&';
        else if (!strncmp(s, "&lt;", 4)) c = '<';
        else if (!strncmp(s, "&gt;", 4)) c = '>';
        else if (!strncmp(s, "&quot;", 6)) c = '"';
        else if (!strncmp(s, "&apos;", 6)) c = '\'';
        else if (!strncmp(s, "&nbsp;", 6)) c = ' ';
        if (!c || c > 0x10FFFF) { *o++ = *s++; continue; }
        o += put_utf8(o, c);
        s = (char *)e + 1;
    }
    *o = 0;
}

static void strip_tags(char *s)
{
    char *o = s;
    bool in = false;
    for (; *s; s++) {
        if (*s == '<') { in = true; *o++ = ' '; }
        else if (*s == '>' && in) in = false;
        else if (!in) *o++ = *s;
    }
    *o = 0;
}

static void collapse(char *s)
{
    char *start = s, *o = s;
    for (bool sp = true; *s; s++) {
        if ((unsigned char)*s <= ' ') { if (!sp) *o++ = ' '; sp = true; }
        else { *o++ = *s; sp = false; }
    }
    if (o != start && o[-1] == ' ') o--;
    *o = 0;
}

/* Rohtext -> Klartext (Entities, Tags, Leerraum) */
static void clean(char *s)
{
    decode(s);
    strip_tags(s);
    decode(s);
    collapse(s);
}

static void cut(char *s, size_t max)   /* auf max-1 Bytes kuerzen, nie mitten im UTF-8-Zeichen */
{
    if (strlen(s) < max) return;
    size_t n = max - 1;
    while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    s[n] = 0;
}

static void begin_item(rss_t *r) { r->in_item = true; r->field = 0; r->rlen[0] = r->rlen[1] = 0; r->raw[0][0] = r->raw[1][0] = 0; }

static void end_item(rss_t *r)
{
    r->in_item = false;
    r->field = 0;
    char *t = r->raw[0], *d = r->raw[1];
    clean(t);
    clean(d);
    cut(t, RSS_TITLE_MAX);
    cut(d, RSS_DESC_MAX);
    if (t[0] && !r->cb(t, d, r->ctx)) r->stopped = true;
}

static void tag_done(rss_t *r)
{
    char *t = r->tag;
    r->tag[r->tlen] = 0;
    bool self = r->tlen && r->tag[r->tlen - 1] == '/';
    bool close = t[0] == '/';
    if (close) t++;
    /* Namensraum (dc:title) und Attribute abschneiden */
    for (char *p = t; *p; p++) if (*p == ' ' || *p == '/') { *p = 0; break; }
    char *c = strchr(t, ':');
    if (c) t = c + 1;
    for (char *p = t; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (!strcmp(t, "item") || !strcmp(t, "entry")) {
        if (close) { if (r->in_item) end_item(r); }
        else if (!self) begin_item(r);
    } else if (r->in_item && !self) {
        int f = !strcmp(t, "title") ? 1 : (!strcmp(t, "description") || !strcmp(t, "summary")) ? 2 : 0;
        if (f && close) r->field = 0;
        else if (f && !r->rlen[f - 1]) r->field = f;
        else if (f) r->field = 0;   /* zweite Beschreibung (z.B. content): ignorieren */
    }
}

static void text_char(rss_t *r, char ch)
{
    if (!r->in_item || !r->field) return;
    int f = r->field - 1;
    if (r->rlen[f] < RSS_RAW_MAX - 1) { r->raw[f][r->rlen[f]++] = ch; r->raw[f][r->rlen[f]] = 0; }
}

bool rss_feed(rss_t *r, const char *d, size_t n)
{
    for (size_t i = 0; i < n && !r->stopped; i++) {
        char ch = d[i];
        switch (r->mode) {
        case M_TEXT:
            if (ch == '<') { r->mode = M_TAG; r->tlen = 0; }
            else text_char(r, ch);
            break;
        case M_TAG:
            if (ch == '>') { r->mode = M_TEXT; tag_done(r); break; }
            if (r->tlen < (int)sizeof r->tag - 1) r->tag[r->tlen++] = ch;
            r->tag[r->tlen] = 0;
            if (r->tlen == 3 && !strncmp(r->tag, "!--", 3)) { r->mode = M_COMMENT; r->last[0] = r->last[1] = 0; }
            else if (r->tlen == 8 && !strncmp(r->tag, "![CDATA[", 8)) { r->mode = M_CDATA; r->last[0] = r->last[1] = 0; }
            break;
        case M_CDATA:
            r->last[2] = ch;
            if (r->last[0] == ']' && r->last[1] == ']' && ch == '>') {
                /* die zwei ']' wurden schon uebernommen: wieder entfernen */
                if (r->in_item && r->field) { int f = r->field - 1; if (r->rlen[f] >= 2) r->raw[f][r->rlen[f] -= 2] = 0; }
                r->mode = M_TEXT;
                break;
            }
            text_char(r, ch);
            r->last[0] = r->last[1]; r->last[1] = ch;
            break;
        case M_COMMENT:
            if (r->last[0] == '-' && r->last[1] == '-' && ch == '>') r->mode = M_TEXT;
            r->last[0] = r->last[1]; r->last[1] = ch;
            break;
        }
    }
    return !r->stopped;
}
