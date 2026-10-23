#include "text.h"
#include <stdbool.h>
#include <string.h>

/* Ein Unicode-Zeichen in darstellbaren Text: leer = weglassen */
static const char *map(unsigned c)
{
    static char one[2];
    if (c >= 'a' && c <= 'z') { one[0] = (char)(c - 32); one[1] = 0; return one; }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { one[0] = (char)c; one[1] = 0; return one; }
    switch (c) {
    case ' ': case 0xA0: return " ";
    case '.': case ',': case ';': return c == ';' ? "," : (c == '.' ? "." : ",");
    case ':': return ":";
    case '!': return "!";
    case '?': return "?";
    case '-': case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
    case '+': return "+";
    case '/': return "/";
    case '%': return "%";
    case '*': return "*";
    case '"': case '\'': case 0x201C: case 0x201D: case 0x201E: case 0x2018: case 0x2019: case 0x201A:
    case 0xAB: case 0xBB: case 0x2039: case 0x203A: case '`': case 0xB4: return "\"";
    case '(': case '[': case '{': return "(";
    case ')': case ']': case '}': return ")";
    case '&': return "+";
    case '=': return "=";
    case '#': return "#";
    case '@': return "@";
    case '<': return "<";
    case '>': return ">";
    case '_': return "-";
    case 0x2026: return "...";
    case 0xC4: case 0xE4: return TEXT_AE;
    case 0xD6: case 0xF6: return TEXT_OE;
    case 0xDC: case 0xFC: return TEXT_UE;
    case 0xDF: case 0x1E9E: return TEXT_SZ;
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC5: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: return "A";
    case 0xC6: case 0xE6: return "AE";
    case 0xC7: case 0xE7: case 0x10C: case 0x10D: case 0x106: case 0x107: return "C";
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0x11A: case 0x11B: return "E";
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0xEC: case 0xED: case 0xEE: case 0xEF: return "I";
    case 0xD1: case 0xF1: case 0x143: case 0x144: return "N";
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xD8: case 0xF8: return "O";
    case 0xD9: case 0xDA: case 0xDB: case 0xF9: case 0xFA: case 0xFB: return "U";
    case 0xDD: case 0xFD: case 0xFF: return "Y";
    case 0x160: case 0x161: case 0x15A: case 0x15B: return "S";
    case 0x17D: case 0x17E: case 0x179: case 0x17A: case 0x17B: case 0x17C: return "Z";
    case 0x141: case 0x142: return "L";
    case 0x20AC: return "EUR";
    case 0xB0: return "";        /* Grad: Zahl bleibt stehen */
    case 0xB2: return "2";
    case 0xB3: return "3";
    }
    return "";   /* Emojis, Pfeile, Schriftzeichen: weg */
}

void text_simplify(char *out, size_t max, const char *s)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && o + 1 < max) {
        unsigned c = *p++;
        if (c >= 0xC0) {   /* Mehrbyte-Zeichen zusammensetzen */
            int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            c &= c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F;
            while (extra-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
        } else if (c >= 0x80) continue;
        if (c == '\n' || c == '\t') c = ' ';
        const char *m = map(c);
        /* keine doppelten Leerzeichen */
        if (m[0] == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        while (*m && o + 1 < max) out[o++] = *m++;
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static bool vowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y'; }
static bool letter(char c) { return c >= 'A' && c <= 'Z'; }

/* Darf das Wort w vor Position i getrennt werden? Einfache deutsche Regel: eine neue Silbe beginnt
 * mit einem Konsonanten vor einem Vokal ("BUN-DES-TAG"). CH, CK, PH, TH und SCH bleiben zusammen;
 * nach einem Bindestrich darf immer getrennt werden. */
static bool can_split(const char *w, int i, int len)
{
    if (i < 3 || len - i < 3) return false;
    if (w[i - 1] == '-') return true;
    char a = w[i - 1], b = w[i], c = w[i + 1];
    if (!letter(a) || !letter(b) || !letter(c)) return false;
    if (b == 'S' && c == 'C' && w[i + 2] == 'H' && a != 'S') return i + 3 < len && vowel(w[i + 3]);   /* DEUT-SCHLAND */
    if (b == 'C' && c == 'H' && a != 'S' && vowel(w[i + 2])) return true;                             /* BUE-CHER */
    if (vowel(b) || !vowel(c)) return false;
    if ((a == 'C' && (b == 'H' || b == 'K')) || ((a == 'P' || a == 'T') && b == 'H')) return false;
    if (b == 'H' && a == 'C') return false;
    if (a == 'S' && b == 'C') return false;                 /* SCH */
    if (i >= 2 && w[i - 2] == 'S' && a == 'C' && b == 'H') return false;
    if (b == 'C' && (c == 'H' || c == 'K')) return true;   /* Trennung vor CH/CK ist ok */
    return true;
}

/* kurze Zahl ("3.", "100", "12,5") - bleibt mit dem folgenden Wort zusammen */
static bool sticky_number(const char *w, int len)
{
    if (len > 5 || len == 0) return false;
    for (int i = 0; i < len; i++) if (!((w[i] >= '0' && w[i] <= '9') || w[i] == '.' || w[i] == ',')) return false;
    return true;
}

int text_wrap(const char *t, int width, char *lines, int line_size, int max_lines)
{
    if (width >= line_size - 1) width = line_size - 2;
    int n = 0, ll = 0;
    char *ln = lines;
    ln[0] = 0;
    while (*t && n < max_lines) {
        while (*t == ' ') t++;
        if (!*t) break;
        int wl = 0;
        while (t[wl] && t[wl] != ' ') wl++;
        /* Zahl + naechstes Wort als Einheit behandeln */
        int unit = wl;
        if (sticky_number(t, wl) && t[wl] == ' ') {
            int nl = 0;
            while (t[wl + 1 + nl] && t[wl + 1 + nl] != ' ') nl++;
            if (nl && wl + 1 + nl <= width) unit = wl + 1 + nl;
        }
        int room = width - ll - (ll ? 1 : 0);
        if (unit <= room) {   /* passt komplett */
            if (ll) ln[ll++] = ' ';
            memcpy(ln + ll, t, unit); ll += unit; ln[ll] = 0;
            t += unit;
            continue;
        }
        /* passt nicht: lange Woerter trennen, wenn am Zeilenende genug Platz ist */
        int split = 0;
        if (wl >= 7 && room >= 4)
            for (int i = room - 1; i >= 3; i--) if (i < wl && can_split(t, i, wl)) { split = i; break; }
        if (!split && wl > width && room >= 3) split = room - 1;   /* ueberlang: hart trennen */
        if (split) {
            if (ll) ln[ll++] = ' ';
            memcpy(ln + ll, t, split); ll += split;
            if (t[split - 1] != '-') ln[ll++] = '-';
            ln[ll] = 0;
            t += split;
        }
        /* neue Zeile */
        if (ll) {
            n++;
            if (n >= max_lines) break;
            ln = lines + n * line_size; ll = 0; ln[0] = 0;
        } else if (!split) {   /* leere Zeile und Wort passt trotzdem nicht: hart trennen */
            memcpy(ln, t, width); ln[width] = 0; t += width;
            n++;
            if (n >= max_lines) break;
            ln = lines + n * line_size; ll = 0; ln[0] = 0;
        }
    }
    if (ll && n < max_lines) n++;
    if (n == max_lines && *t) {   /* passt nicht mehr: Auslassungspunkte */
        char *last = lines + (n - 1) * line_size;
        int l = (int)strlen(last);
        if (l > width - 3) l = width - 3;
        while (l > 0 && last[l - 1] == ' ') l--;
        strcpy(last + l, "...");
    }
    return n;
}
