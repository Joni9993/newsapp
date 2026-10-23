/* Bounded streaming iCalendar reader. Unsupported recurrence/timezones fail
 * explicitly. Time conversions are independent of the C library's process-wide
 * timezone. */
#include "ical.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  int y, m, d, h, mi, s, zone;
  bool all;
} date_t;
/* Klein gehalten: der Parser liegt waehrend des HTTPS-Abrufs im Heap */
typedef struct {
  char uid[128], title[128], location[64], rule[192];
  date_t start, end, rid;
  bool has_start, has_end, override, cancelled;
  int sequence;
  int64_t ex[16], rdates[8];
  int nex, nr;
} event_t;
struct ical_parser {
  event_t events[ICAL_MAX_EVENTS], cur;
  int n, dropped;
  int64_t now; /* 0 = kein Zeitfenster */
  char line[1024], physical[1024], error[100];
  size_t len, plen, total;
  bool in_event, in_calendar, closed, trunc, ptrunc;
  int nested;
};
static bool fail(ical_parser_t *p, const char *s) {
  snprintf(p->error, sizeof p->error, "%s", s);
  return false;
}
const char *ical_error(const ical_parser_t *p) { return p->error; }
ical_parser_t *ical_create(void) { return calloc(1, sizeof(ical_parser_t)); }
void ical_set_window(ical_parser_t *p, int64_t now) { p->now = now; }
int ical_dropped(const ical_parser_t *p) { return p->dropped; }
void ical_free(ical_parser_t *p) { free(p); }
static int64_t days(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yo = y - era * 400;
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097LL + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
}
static void civil(int64_t z, date_t *d) {
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = z - era * 146097,
           yo = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = yo + era * 400;
  unsigned dy = doe - (365 * yo + yo / 4 - yo / 100), mp = (5 * dy + 2) / 153;
  d->d = dy - (153 * mp + 2) / 5 + 1;
  d->m = mp + (mp < 10 ? 3 : -9);
  d->y = y + (d->m <= 2);
}
static int monthdays(int y, int m) {
  static const int ds[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return ds[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
static int weekday(int y, int m, int d) {
  int w = (days(y, m, d) + 4) % 7;
  return w < 0 ? w + 7 : w;
}
static int last_sunday(int y, int m) {
  int n = monthdays(y, m);
  return n - weekday(y, m, n);
}
static int nth_sunday(int y, int m, int n) {
  return 1 + (7 - weekday(y, m, 1)) % 7 + 7 * (n - 1);
}
static int64_t wall(date_t d) {
  return days(d.y, d.m, d.d) * 86400 + d.h * 3600 + d.mi * 60 + d.s;
}
static int offset(date_t d) {
  if (d.zone == 0)
    return 0;
  if (d.zone == 1) {
    bool dst = d.m > 3 && d.m < 10;
    if (d.m == 3)
      dst =
          d.d > last_sunday(d.y, 3) || (d.d == last_sunday(d.y, 3) && d.h >= 2);
    if (d.m == 10)
      dst = d.d < last_sunday(d.y, 10) ||
            (d.d == last_sunday(d.y, 10) && d.h < 3);
    return dst ? 7200 : 3600;
  }
  if (d.zone == 2) {
    bool dst = d.m > 3 && d.m < 10;
    if (d.m == 3)
      dst =
          d.d > last_sunday(d.y, 3) || (d.d == last_sunday(d.y, 3) && d.h >= 1);
    if (d.m == 10)
      dst = d.d < last_sunday(d.y, 10) ||
            (d.d == last_sunday(d.y, 10) && d.h < 2);
    return dst ? 3600 : 0;
  }
  int base = d.zone == 3 ? -5 : d.zone == 4 ? -6 : d.zone == 5 ? -7 : -8;
  bool dst = d.m > 3 && d.m < 11;
  if (d.m == 3)
    dst = d.d > nth_sunday(d.y, 3, 2) ||
          (d.d == nth_sunday(d.y, 3, 2) && d.h >= 2);
  if (d.m == 11)
    dst = d.d < nth_sunday(d.y, 11, 1) ||
          (d.d == nth_sunday(d.y, 11, 1) && d.h < 2);
  return (base + (dst ? 1 : 0)) * 3600;
}
static int64_t utc(date_t d) { return wall(d) - offset(d); }
static int zone(const char *z) {
  if (!strcmp(z, "UTC") || !strcmp(z, "Etc/UTC") || !strcmp(z, "GMT"))
    return 0;
  const char *eu[] = {"Europe/Berlin",   "Europe/Paris",  "Europe/Rome",
                      "Europe/Vienna",   "Europe/Zurich", "Europe/Amsterdam",
                      "Europe/Brussels", "Europe/Madrid", "Europe/Prague",
                      "Europe/Warsaw"};
  for (unsigned i = 0; i < sizeof eu / sizeof *eu; i++)
    if (!strcmp(z, eu[i]))
      return 1;
  if (!strcmp(z, "Europe/London"))
    return 2;
  if (!strcmp(z, "America/New_York"))
    return 3;
  if (!strcmp(z, "America/Chicago"))
    return 4;
  if (!strcmp(z, "America/Denver"))
    return 5;
  if (!strcmp(z, "America/Los_Angeles"))
    return 6;
  return -1;
}
static bool date(ical_parser_t *p, const char *prop, const char *v, date_t *d,
                 int fallback) {
  memset(d, 0, sizeof *d);
  d->zone = fallback;
  size_t len = strlen(v);
  if (len != 8 && len != 15 && len != 16)
    return fail(p, "Ungültiges Kalenderdatum");
  for (size_t i = 0; i < len; i++)
    if (i != 8 && i != 15 && !isdigit((unsigned char)v[i]))
      return fail(p, "Ungültiges Kalenderdatum");
  if (sscanf(v, "%4d%2d%2d", &d->y, &d->m, &d->d) != 3 || d->y < 1970 ||
      d->y > 2100 || d->m < 1 || d->m > 12 || d->d < 1 ||
      d->d > monthdays(d->y, d->m))
    return fail(p, "Kalenderdatum außerhalb 1970–2100");
  d->all = len == 8;
  if (!d->all &&
      (v[8] != 'T' || sscanf(v + 9, "%2d%2d%2d", &d->h, &d->mi, &d->s) != 3 ||
       d->h > 23 || d->mi > 59 || d->s > 59))
    return fail(p, "Ungültige Kalenderzeit");
  const char *t = strstr(prop, "TZID=");
  if (t) {
    char z[80];
    t += 5;
    int n = 0;
    if (*t == '\"')
      t++;
    while (*t && *t != ';' && *t != '\"' && n < 79)
      z[n++] = *t++;
    z[n] = 0;
    d->zone = zone(z);
    if (d->zone < 0)
      return fail(p, "Zeitzone nicht unterstützt; Kalender in Europe/Berlin "
                     "oder UTC verwenden");
  }
  if (len == 16) {
    if (v[15] != 'Z')
      return fail(p, "Ungültige UTC-Zeit");
    d->zone = 0;
  }
  return true;
}
static void unescape(char *out, size_t n, const char *v) {
  size_t k = 0;
  while (*v && k + 1 < n) {
    if (*v == '\\' && v[1]) {
      v++;
      out[k++] = (*v == 'n' || *v == 'N') ? ' ' : *v;
      v++;
    } else
      out[k++] = *v++;
  }
  out[k] = 0;
}
static bool plain(const event_t *e) {
  return !e->rule[0] && !e->nr && !e->override;
}
/* Kann der Termin im Fenster [jetzt - 1 Tag, jetzt + 8 Tage] vorkommen oder
 * dort etwas veraendern? Google-Feeds enthalten die ganze Historie. */
static bool relevant(const ical_parser_t *p, const event_t *e) {
  if (!p->now)
    return true;
  int64_t from = p->now - 86400, to = p->now + 8 * 86400;
  bool timed = e->has_start;
  int64_t s = timed ? utc(e->start) : 0, en = timed ? utc(e->end) : 0;
  if (e->override) {
    int64_t r = utc(e->rid);
    return (r >= from && r < to) || !timed || (en >= from && s < to);
  }
  if (!timed)
    return true;
  if (e->rule[0]) {
    const char *u = strstr(e->rule, "UNTIL=");
    int y, m, d;
    if (u && sscanf(u + 6, "%4d%2d%2d", &y, &m, &d) == 3 &&
        (days(y, m, d) + 1) * 86400 < from)
      return false;
    return s < to;
  }
  bool any = en >= from && s < to;
  for (int i = 0; i < e->nr; i++)
    if (e->rdates[i] + (en - s) >= from && e->rdates[i] < to)
      any = true;
  return any;
}
/* Ausnahmeliste voll: veraltete Eintraege duerfen Platz machen */
static bool add_date(ical_parser_t *p, int64_t *list, int *count, int cap,
                     int64_t at) {
  if (*count < cap) {
    list[(*count)++] = at;
    return true;
  }
  if (!p->now)
    return false;
  int64_t from = p->now - 86400;
  if (at < from)
    return true;
  for (int i = 0; i < cap; i++)
    if (list[i] < from) {
      list[i] = at;
      return true;
    }
  return false;
}
static bool line(ical_parser_t *p) {
  char *s = p->line;
  char *v = strchr(s, ':');
  if (!v)
    return !*s || fail(p, "Ungültige ICS-Zeile");
  *v++ = 0;
  if (!strcmp(s, "BEGIN") && !strcmp(v, "VCALENDAR")) {
    p->in_calendar = true;
    return true;
  }
  if (!strcmp(s, "END") && !strcmp(v, "VCALENDAR")) {
    p->closed = true;
    return true;
  }
  if (!strcmp(s, "BEGIN") && !strcmp(v, "VEVENT")) {
    if (p->in_event)
      return fail(p, "Verschachtelter Termin");
    memset(&p->cur, 0, sizeof p->cur);
    p->in_event = true;
    p->nested = 0;
    return true;
  }
  if (!p->in_event)
    return true;
  if (!strcmp(s, "END") && !strcmp(v, "VEVENT")) {
    event_t *e = &p->cur;
    if (!e->uid[0] || (!e->has_start && !e->cancelled))
      return fail(p, "Termin ohne UID oder Start");
    if (!e->has_end) {
      e->end = e->start;
      if (e->start.all)
        civil(days(e->start.y, e->start.m, e->start.d) + 1, &e->end);
    }
    if (e->has_start && utc(e->end) < utc(e->start))
      return fail(p, "Terminende liegt vor dem Beginn");
    p->in_event = false;
    if (!relevant(p, e))
      return true;
    if (p->n < ICAL_MAX_EVENTS) {
      p->events[p->n++] = *e;
      return true;
    }
    /* Voll: der spaeteste Einzeltermin weicht, Serien bleiben */
    p->dropped++;
    int victim = -1;
    for (int i = 0; i < p->n; i++)
      if (plain(&p->events[i]) &&
          (victim < 0 ||
           utc(p->events[i].start) > utc(p->events[victim].start)))
        victim = i;
    if (victim >= 0 &&
        (!plain(e) || utc(e->start) < utc(p->events[victim].start)))
      p->events[victim] = *e;
    return true;
  }
  if (!strcmp(s, "BEGIN")) {
    p->nested++;
    return true;
  }
  if (!strcmp(s, "END")) {
    p->nested--;
    return true;
  }
  if (p->nested)
    return true;
  event_t *e = &p->cur;
  char name[40];
  size_t n = strcspn(s, ";");
  if (n >= sizeof name)
    return true;
  memcpy(name, s, n);
  name[n] = 0;
  /* Gekuerzte Zeile: Titel/Ort duerfen gekuerzt werden, Beschreibungen sind
   * egal, alles Strukturelle muss vollstaendig sein */
  if (p->trunc && strcmp(name, "SUMMARY") && strcmp(name, "LOCATION")) {
    if (!strcmp(name, "UID") || !strcmp(name, "DTSTART") ||
        !strcmp(name, "DTEND") || !strcmp(name, "RRULE") ||
        !strcmp(name, "EXDATE") || !strcmp(name, "RDATE") ||
        !strcmp(name, "RECURRENCE-ID"))
      return fail(p, "Kalenderzeile zu lang (maximal 1023 Bytes)");
    return true;
  }
  if (!strcmp(name, "UID")) {
    if (strlen(v) >= sizeof e->uid)
      return fail(p, "Kalender-UID zu lang");
    strcpy(e->uid, v);
  } else if (!strcmp(name, "SUMMARY"))
    unescape(e->title, sizeof e->title, v);
  else if (!strcmp(name, "LOCATION"))
    unescape(e->location, sizeof e->location, v);
  else if (!strcmp(name, "DTSTART")) {
    if (!date(p, s, v, &e->start, 1))
      return false;
    e->has_start = true;
  } else if (!strcmp(name, "DTEND")) {
    if (!date(p, s, v, &e->end, 1))
      return false;
    e->has_end = true;
  } else if (!strcmp(name, "RECURRENCE-ID")) {
    if (strstr(s, "RANGE="))
      return fail(p, "Serienänderung THISANDFUTURE nicht unterstützt");
    if (!date(p, s, v, &e->rid, 1))
      return false;
    e->override = true;
  } else if (!strcmp(name, "STATUS"))
    e->cancelled = !strcmp(v, "CANCELLED");
  else if (!strcmp(name, "SEQUENCE"))
    e->sequence = atoi(v);
  else if (!strcmp(name, "RRULE")) {
    if (strlen(v) >= sizeof e->rule)
      return fail(p, "Wiederholungsregel zu lang");
    strcpy(e->rule, v);
  } else if (!strcmp(name, "DURATION"))
    return fail(p, "DURATION nicht unterstützt; Kalender mit DTEND verwenden");
  else if (!strcmp(name, "EXDATE") || !strcmp(name, "RDATE")) {
    bool ex = !strcmp(name, "EXDATE");
    char *save = NULL;
    for (char *t = strtok_r(v, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
      date_t d;
      if (!date(p, s, t, &d, 1))
        return false;
      if (!(ex ? add_date(p, e->ex, &e->nex, 16, utc(d))
               : add_date(p, e->rdates, &e->nr, 8, utc(d))))
        return fail(p, "Zu viele Serienausnahmen");
    }
  }
  return true;
}
static bool physical(ical_parser_t *p) {
  p->physical[p->plen] = 0;
  bool fold = p->plen && (p->physical[0] == ' ' || p->physical[0] == '\t');
  if (!fold && (p->len || p->trunc)) {
    p->line[p->len] = 0;
    if (!line(p))
      return false;
    p->len = 0;
    p->trunc = false;
  }
  if (p->ptrunc)
    p->trunc = true;
  p->ptrunc = false;
  size_t start = fold ? 1 : 0, n = p->plen - start;
  if (p->len + n >= sizeof p->line) {
    n = sizeof p->line - 1 - p->len;
    p->trunc = true;
  }
  memcpy(p->line + p->len, p->physical + start, n);
  p->len += n;
  p->plen = 0;
  return true;
}
bool ical_feed(ical_parser_t *p, const char *data, size_t n) {
  if (p->error[0])
    return false;
  p->total += n;
  if (p->total > ICAL_MAX_BYTES)
    return fail(p, "Kalender größer als 4 MiB");
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\r')
      continue;
    if (c == '\n') {
      if (!physical(p))
        return false;
    } else {
      if (p->plen + 1 >= sizeof p->physical)
        p->ptrunc = true;
      else
        p->physical[p->plen++] = c;
    }
  }
  return true;
}
typedef struct {
  int freq, interval, count, month, monthday, wkst;
  int byday[14], ordinal[14], nd;
  int64_t until;
} rule_t;
static int daynum(const char *s) {
  static const char *ds[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  for (int i = 0; i < 7; i++)
    if (!strcmp(s, ds[i]))
      return i;
  return -1;
}
static bool rule(ical_parser_t *p, event_t *e, rule_t *r) {
  memset(r, 0, sizeof *r);
  r->interval = 1;
  r->count = 100000;
  r->until = INT64_MAX;
  r->wkst = 1;
  char b[sizeof e->rule];
  strcpy(b, e->rule);
  char *save = NULL;
  for (char *t = strtok_r(b, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
    char *v = strchr(t, '=');
    if (!v)
      return fail(p, "Ungültige Wiederholungsregel");
    *v++ = 0;
    if (!strcmp(t, "FREQ")) {
      r->freq = !strcmp(v, "DAILY")     ? 1
                : !strcmp(v, "WEEKLY")  ? 2
                : !strcmp(v, "MONTHLY") ? 3
                : !strcmp(v, "YEARLY")  ? 4
                                        : 0;
      if (!r->freq)
        return fail(p, "Wiederholungsfrequenz nicht unterstützt");
    } else if (!strcmp(t, "INTERVAL")) {
      r->interval = atoi(v);
      if (r->interval < 1 || r->interval > 366)
        return fail(p, "Ungültiges Serienintervall");
    } else if (!strcmp(t, "COUNT")) {
      r->count = atoi(v);
      if (r->count < 1 || r->count > 100000)
        return fail(p, "Ungültige Serienanzahl");
    } else if (!strcmp(t, "UNTIL")) {
      date_t d;
      if (!date(p, "", v, &d, e->start.zone))
        return false;
      r->until = utc(d);
    } else if (!strcmp(t, "BYMONTH")) {
      if (strchr(v, ','))
        return fail(p, "Mehrere BYMONTH-Werte nicht unterstützt");
      r->month = atoi(v);
      if (r->month < 1 || r->month > 12)
        return fail(p, "Ungültiger Serienmonat");
    } else if (!strcmp(t, "BYMONTHDAY")) {
      if (strchr(v, ','))
        return fail(p, "Mehrere BYMONTHDAY-Werte nicht unterstützt");
      r->monthday = atoi(v);
      if (!r->monthday || r->monthday < -31 || r->monthday > 31)
        return fail(p, "Ungültiger Serientag");
    } else if (!strcmp(t, "WKST")) {
      r->wkst = daynum(v);
      if (r->wkst < 0)
        return fail(p, "Ungültiger Wochenbeginn");
    } else if (!strcmp(t, "BYDAY")) {
      char *ds = NULL;
      for (char *d = strtok_r(v, ",", &ds); d; d = strtok_r(NULL, ",", &ds)) {
        if (r->nd >= 14)
          return fail(p, "Zu viele Serienwochentage");
        int ord = 0;
        if (*d == '-' || *d == '+' || isdigit((unsigned char)*d)) {
          ord = strtol(d, &d, 10);
          if (!ord || ord < -5 || ord > 5)
            return fail(p, "Ungültiger Serienwochentag");
        }
        int wd = daynum(d);
        if (wd < 0)
          return fail(p, "Ungültiger Serienwochentag");
        r->byday[r->nd] = wd;
        r->ordinal[r->nd++] = ord;
      }
    } else
      return fail(p, "Wiederholungsregel nicht unterstützt (z.B. BYSETPOS)");
  }
  if (!r->freq)
    return fail(p, "Serienfrequenz fehlt");
  for (int i = 0; i < r->nd; i++)
    if (r->ordinal[i] && (r->freq < 3 || (r->freq == 4 && !r->month)))
      return fail(p, "Diese ordinalen Wochentage werden nicht unterstützt");
  return true;
}
static bool matches(date_t d, date_t start, rule_t *r) {
  int64_t delta = days(d.y, d.m, d.d) - days(start.y, start.m, start.d);
  int wd = weekday(d.y, d.m, d.d),
      months = (d.y - start.y) * 12 + d.m - start.m;
  if (r->freq == 1 && delta % r->interval)
    return false;
  if (r->freq == 2 &&
      ((delta + (weekday(start.y, start.m, start.d) - r->wkst + 7) % 7) / 7) %
          r->interval)
    return false;
  if (r->freq == 3 && months % r->interval)
    return false;
  if (r->freq == 4 && (d.y - start.y) % r->interval)
    return false;
  if (r->month && d.m != r->month)
    return false;
  if (r->freq == 4 && !r->month && d.m != start.m)
    return false;
  if (r->monthday) {
    int want =
        r->monthday > 0 ? r->monthday : monthdays(d.y, d.m) + 1 + r->monthday;
    if (d.d != want)
      return false;
  }
  if (r->nd) {
    bool found = false;
    for (int i = 0; i < r->nd; i++) {
      int o = r->ordinal[i];
      if (wd == r->byday[i] &&
          (!o || (o > 0 && (d.d - 1) / 7 + 1 == o) ||
           (o < 0 && -((monthdays(d.y, d.m) - d.d) / 7 + 1) == o)))
        found = true;
    }
    if (!found)
      return false;
  } else if (r->freq == 2 && wd != weekday(start.y, start.m, start.d))
    return false;
  else if (r->freq >= 3 && !r->monthday && d.d != start.d)
    return false;
  return true;
}
static event_t * override(ical_parser_t *p, event_t *base, int64_t at) {
  event_t *best = NULL;
  for (int i = 0; i < p->n; i++) {
    event_t *e = &p->events[i];
    if (e->override && !strcmp(e->uid, base->uid) && utc(e->rid) == at &&
        (!best || e->sequence >= best->sequence))
      best = e;
  }
  return best;
}
static void insert(ical_occurrence_t *out, int *n, event_t *e, int64_t at,
                   int64_t end, int64_t now) {
  if (end <= now && at < now)
    return;
  if (at >= now + 7 * 86400 || e->cancelled)
    return;
  int pos = 0;
  while (pos < *n && out[pos].start <= at)
    pos++;
  if (pos >= ICAL_MAX_RESULTS)
    return;
  if (*n < ICAL_MAX_RESULTS)
    (*n)++;
  memmove(out + pos + 1, out + pos, (*n - pos - 1) * sizeof *out);
  ical_occurrence_t *o = &out[pos];
  o->start = at;
  o->end = end;
  o->all_day = e->start.all;
  snprintf(o->title, sizeof o->title, "%s", e->title[0] ? e->title : "Termin");
  snprintf(o->location, sizeof o->location, "%s", e->location);
}
static void occurrence(ical_parser_t *p, event_t *e, date_t d,
                       ical_occurrence_t *out, int *n, int64_t now) {
  int64_t at = utc(d);
  for (int i = 0; i < e->nex; i++)
    if (e->ex[i] == at)
      return;
  if (override(p, e, at))
    return;
  date_t finish = d;
  int64_t span = wall(e->end) - wall(e->start);
  int64_t w = wall(d) + span;
  civil(w / 86400, &finish);
  int sec = w % 86400;
  finish.h = sec / 3600;
  finish.mi = sec / 60 % 60;
  finish.s = sec % 60;
  insert(out, n, e, at, utc(finish), now);
}
bool ical_finish(ical_parser_t *p, int64_t now, ical_occurrence_t *out,
                 int *count) {
  *count = 0;
  if (p->error[0])
    return false;
  if (p->plen && !physical(p))
    return false;
  if (p->len || p->trunc) {
    p->line[p->len] = 0;
    if (!line(p))
      return false;
    p->len = 0;
    p->trunc = false;
  }
  if (!p->in_calendar || !p->closed || p->in_event)
    return fail(p, "Unvollständiger ICS-Kalender");
  for (int i = 0; i < p->n; i++) {
    event_t *e = &p->events[i];
    if (e->cancelled)
      continue;
    bool newer = false;
    for (int j = 0; j < p->n; j++)
      if (j != i && !strcmp(e->uid, p->events[j].uid) &&
          e->override == p->events[j].override &&
          (!e->override || utc(e->rid) == utc(p->events[j].rid)) &&
          (p->events[j].sequence > e->sequence ||
           (p->events[j].sequence == e->sequence && j > i)))
        newer = true;
    if (newer)
      continue;
    if (e->override) {
      insert(out, count, e, utc(e->start), utc(e->end), now);
      continue;
    }
    if (!e->rule[0])
      occurrence(p, e, e->start, out, count, now);
    else {
      rule_t r;
      if (!rule(p, e, &r))
        return false;
      int seen = 0;
      int64_t first = days(e->start.y, e->start.m, e->start.d),
              last = (now + 8 * 86400) / 86400;
      if (last - first > 50000)
        return fail(p, "Serie zu alt für die begrenzte Berechnung");
      for (int64_t dd = first; dd <= last; dd++) {
        date_t d = e->start;
        civil(dd, &d);
        int64_t at = utc(d);
        if (at > r.until)
          break;
        if (matches(d, e->start, &r)) {
          if (++seen > r.count)
            break;
          occurrence(p, e, d, out, count, now);
        }
      }
    }
    for (int j = 0; j < e->nr; j++) {
      int64_t at = e->rdates[j];
      bool excluded = false;
      for (int k = 0; k < e->nex; k++)
        if (e->ex[k] == at)
          excluded = true;
      if (!excluded && !override(p, e, at))
        insert(out, count, e, at + 0, at + utc(e->end) - utc(e->start), now);
    }
  }
  return true;
}
