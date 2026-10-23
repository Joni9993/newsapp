/* Push-Nachrichten vom Handy: eine Automatisierungs-App (z. B. MacroDroid)
 * schickt App, Absender und Text an /api/v1/notify; das Board zeigt sie sofort.
 * Einstellungen liegen in eigenen NVS-Schluesseln (nicht im Konfig-Blob). */
#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

enum { NOTIFY_FULL, NOTIFY_SENDER, NOTIFY_APP_ONLY };   /* was angezeigt wird */
enum { NK_BELL, NK_CHAT, NK_MAIL, NK_CALL };             /* Art der Nachricht */

typedef struct {
  char app[32], title[80], text[480];
  time_t at;
  int kind;
} notify_t;

void notify_init(void);
/* false: ausgeschaltet oder doppelt; why sagt warum */
bool notify_push(const char *app, const char *title, const char *text,
                 const char **why);
bool notify_pop(notify_t *out);
bool notify_token_ok(const char *token);
const char *notify_token(void);
cJSON *notify_public(void);
bool notify_update(const cJSON *j);
