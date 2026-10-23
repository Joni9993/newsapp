#include "json.h"
#include <string.h>

enum { S_FREE, S_STR, S_LIT };

void json_init(json_t *j, json_cb cb, void *ctx)
{
    memset(j, 0, sizeof *j);
    j->cb = cb;
    j->ctx = ctx;
}

static bool in_object(const json_t *j) { return j->depth > 0 && j->stack[j->depth - 1] == '{'; }

static void put(json_t *j, char c)
{
    if (j->blen < JSON_VAL_MAX - 1) j->buf[j->blen++] = c;
}

static void put_utf8(json_t *j, unsigned c)
{
    if (c < 0x80) put(j, (char)c);
    else if (c < 0x800) { put(j, (char)(0xC0 | (c >> 6))); put(j, (char)(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) {
        put(j, (char)(0xE0 | (c >> 12))); put(j, (char)(0x80 | ((c >> 6) & 0x3F))); put(j, (char)(0x80 | (c & 0x3F)));
    } else {
        put(j, (char)(0xF0 | (c >> 18))); put(j, (char)(0x80 | ((c >> 12) & 0x3F)));
        put(j, (char)(0x80 | ((c >> 6) & 0x3F))); put(j, (char)(0x80 | (c & 0x3F)));
    }
}

/* Ein Wert ist fertig: im Objekt entweder der Schluessel (der Wert kommt nach ':') oder der Wert */
static void token_done(json_t *j, bool is_string)
{
    j->buf[j->blen] = 0;
    j->state = S_FREE;
    if (is_string && in_object(j) && j->expect_key) {
        size_t l = strlen(j->buf);
        if (l > sizeof j->key - 1) l = sizeof j->key - 1;
        memcpy(j->key, j->buf, l);
        j->key[l] = 0;
        j->expect_key = false;
        j->have_key = true;
        return;
    }
    const char *k = in_object(j) ? (j->have_key ? j->key : "") : (j->depth > 0 && j->depth <= JSON_DEPTH ? j->ckey[j->depth - 1] : "");
    if (!j->cb(k, j->buf, j->depth, j->ctx)) j->stopped = true;
    j->have_key = false;
}

bool json_feed(json_t *j, const char *data, size_t n)
{
    for (size_t i = 0; i < n && !j->stopped; i++) {
        char c = data[i];
        if (j->state == S_STR) {
            if (j->uni) {
                int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
                j->ucode = j->ucode * 16 + (unsigned)v;
                if (--j->uni == 0) {
                    if (j->ucode >= 0xD800 && j->ucode < 0xDC00) j->uhigh = j->ucode;   /* erste Haelfte eines Paars */
                    else if (j->ucode >= 0xDC00 && j->ucode < 0xE000 && j->uhigh)
                        { put_utf8(j, 0x10000 + ((j->uhigh - 0xD800) << 10) + (j->ucode - 0xDC00)); j->uhigh = 0; }
                    else put_utf8(j, j->ucode);
                }
                continue;
            }
            if (j->esc) {
                j->esc = false;
                switch (c) {
                case 'n': case 't': put(j, ' '); break;
                case 'r': case 'b': case 'f': break;
                case 'u': j->uni = 4; j->ucode = 0; break;
                default: put(j, c); break;   /* \" \\ \/ */
                }
                continue;
            }
            if (c == '\\') { j->esc = true; continue; }
            if (c == '"') { token_done(j, true); continue; }
            put(j, c);
            continue;
        }
        if (j->state == S_LIT) {
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ':')
                token_done(j, false);   /* und das Trennzeichen unten weiter verarbeiten */
            else { put(j, c); continue; }
        }
        switch (c) {
        case '{': case '[':
            if (j->depth < JSON_DEPTH) {
                j->stack[j->depth] = c;
                const char *k = j->have_key ? j->key : "";
                size_t l = strlen(k);
                if (l > sizeof j->ckey[0] - 1) l = sizeof j->ckey[0] - 1;
                memcpy(j->ckey[j->depth], k, l);
                j->ckey[j->depth][l] = 0;
            }
            j->depth++;
            j->expect_key = c == '{';
            j->have_key = false;
            break;
        case '}': case ']':
            if (j->depth > 0) j->depth--;
            j->have_key = false;
            j->expect_key = false;
            break;
        case ',':
            j->expect_key = in_object(j);
            j->have_key = false;
            break;
        case ':': break;
        case '"': j->state = S_STR; j->blen = 0; j->esc = false; j->uni = 0; j->uhigh = 0; break;
        case ' ': case '\n': case '\r': case '\t': break;
        default: j->state = S_LIT; j->blen = 0; put(j, c); break;
        }
    }
    return !j->stopped;
}
