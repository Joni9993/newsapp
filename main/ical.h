#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ICAL_MAX_EVENTS 32
#define ICAL_MAX_BYTES (4 * 1024 * 1024)
#define ICAL_MAX_RESULTS 20
typedef struct { int64_t start,end; bool all_day; char title[128],location[96]; } ical_occurrence_t;
typedef struct ical_parser ical_parser_t;
ical_parser_t *ical_create(void);
bool ical_feed(ical_parser_t *p,const char *data,size_t n);
bool ical_finish(ical_parser_t *p,int64_t now,ical_occurrence_t *out,int *count);
const char *ical_error(const ical_parser_t *p);
void ical_free(ical_parser_t *p);
/* Nur Termine behalten, die um 'now' herum (-1 bis +8 Tage) eine Rolle spielen */
void ical_set_window(ical_parser_t *p,int64_t now);
/* Wie viele Termine mangels Platz verworfen wurden */
int ical_dropped(const ical_parser_t *p);
