#include "screen.h"
#include "cards.h"
#include "config.h"
#include "courier.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gfx.h"
#include "lcd.h"
#include "modules.h"
#include "net.h"
#include "sources.h"
#include "text.h"
#include "tty.h"
#include "vendor/qrcodegen.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
static QueueHandle_t commands;
static bool paused, have_card;
static board_config_t cfg;
static uint32_t revision;
static card_t card;
static unsigned ticks, page, hold;
/* laufende Push-Nachricht: eigene Karte und Zeilen, die normale Karte bleibt */
static notify_t note;
static card_t note_card;
static char note_lines[32][26];
static int note_count;
static bool noting;
static unsigned note_ticks, note_hold;
static char lines[32][26];
static int line_count;
static char pin[7];
static int64_t pair_expires;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
enum { NEXT = 1, PREVIOUS, PAUSE, RESUME, CONFIG, PAIR, CLOSE };
bool screen_command(const char *s) {
  int c = !strcmp(s, "next")         ? NEXT
          : !strcmp(s, "previous")   ? PREVIOUS
          : !strcmp(s, "pause")      ? PAUSE
          : !strcmp(s, "resume")     ? RESUME
          : !strcmp(s, "config")     ? CONFIG
          : !strcmp(s, "pair")       ? PAIR
          : !strcmp(s, "closeSetup") ? CLOSE
                                     : 0;
  return c && commands && xQueueSend(commands, &c, 0) == pdTRUE;
}
bool screen_paused(void) { return __atomic_load_n(&paused, __ATOMIC_RELAXED); }
void screen_pair_code(char out[7]) {
  portENTER_CRITICAL(&mux);
  snprintf(out, 7, "%s", pair_expires > esp_timer_get_time() ? pin : "");
  portEXIT_CRITICAL(&mux);
}
void screen_show_pair(void) { screen_command("pair"); }
static void pairing(void) {
  portENTER_CRITICAL(&mux);
  if (pair_expires < esp_timer_get_time()) {
    snprintf(pin, sizeof pin, "%06u", (unsigned)(esp_random() % 1000000));
    pair_expires = esp_timer_get_time() + 300000000;
  }
  portEXIT_CRITICAL(&mux);
}
static void reset_view(void) {
  have_card = false;
  ticks = page = 0;
  courier_reset();
}
static void next_card(void) {
  have_card = cards_next(&card);
  ticks = page = 0;
  line_count = 0;
  if (!have_card)
    return;
  if (card.kind == CARD_WEATHER) {
    weather_t w;
    sources_weather(&w);
    char b[180];
    snprintf(b, sizeof b,
             "%.1f GRAD / WIND %.0f KM/H\nHEUTE %.0f BIS %.0f GRAD\nMORGEN "
             "%.0f BIS %.0f GRAD\nREGENRISIKO %d%%",
             w.temp, w.wind, w.tmin[0], w.tmax[0], w.tmin[1], w.tmax[1],
             w.rain[0]);
    line_count = text_wrap(b, TTY_COLS, &lines[0][0], 26, 32);
  } else {
    if (card.big[0]) {
      snprintf(lines[line_count++], 26, "%s", card.big);
    }
    for (int i = 0; i < card.nparts && line_count < 32; i++) {
      char b[CARD_TEXT_MAX];
      int n = card.part[i].len;
      memcpy(b, card.text + card.part[i].off, n);
      b[n] = 0;
      line_count +=
          text_wrap(b, TTY_COLS, &lines[line_count][0], 26, 32 - line_count);
    }
  }
  /* Textkarten tippen sich ganz durch und scrollen; danach kurz stehen lassen */
  hold = cfg.duration * 30;
  if (card.kind == CARD_TEXT) {
    unsigned chars = strlen(card.head) + 1;
    for (int i = 0; i < line_count; i++)
      chars += strlen(lines[i]) + 1;
    chars = chars * 30 / TTY_CPS + 90;
    if (chars > hold)
      hold = chars;
  }
}
static void start_note(void) {
  char head[24];
  snprintf(head, sizeof head, "%.20s", note.app);
  card_begin(&note_card, CARD_TEXT, SRC_HALLO, head);
  if (note.title[0])
    card_part(&note_card, note.title, RGB(255, 230, 150));
  if (note.text[0])
    card_part(&note_card, note.text, RGB(255, 255, 255));
  if (!note.title[0] && !note.text[0])
    card_part(&note_card, "(Inhalt ausgeblendet)", RGB(255, 255, 255));
  card_finish(&note_card);
  note_count = 0;
  unsigned chars = strlen(note_card.head) + 1;
  for (int i = 0; i < note_card.nparts && note_count < 32; i++) {
    char b[CARD_TEXT_MAX];
    int n = note_card.part[i].len;
    memcpy(b, note_card.text + note_card.part[i].off, n);
    b[n] = 0;
    note_count += text_wrap(b, TTY_COLS, &note_lines[note_count][0], 26,
                            32 - note_count);
  }
  for (int i = 0; i < note_count; i++)
    chars += strlen(note_lines[i]) + 1;
  /* durchtippen lassen, dann 8 s stehen; mindestens 10 s */
  note_hold = chars * 30 / TTY_CPS + 240;
  if (note_hold < 300)
    note_hold = 300;
  note_ticks = 0;
  noting = true;
  if (cfg.mode == 0)
    courier_note(&note_card, note.kind);
}
void screen_init(void) {
  commands = xQueueCreate(8, sizeof(int));
  config_get(&cfg);
  revision = config_revision();
  courier_init();
  courier_set_theme(THEME_FIGURE(cfg.theme));
  /* Code nur bei der Ersteinrichtung von selbst; sonst fordert ihn die Webseite
   * an, sobald jemand sie ohne Anmeldung oeffnet */
  if (!net_configured())
    pairing();
  uint8_t temp[sizeof qr];
  char content[120];
  snprintf(content, sizeof content, "WIFI:T:WPA;S:%s;P:%s;;", net_ap_name(),
           net_ap_password());
  qrcodegen_encodeText(content, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 5,
                       qrcodegen_Mask_AUTO, true);
}
void screen_tick(void) {
  int c;
  while (xQueueReceive(commands, &c, 0) == pdTRUE) {
    switch (c) {
    case NEXT:
      reset_view();
      break;
    case PREVIOUS:
      cards_previous();
      reset_view();
      break;
    case PAUSE:
      __atomic_store_n(&paused, true, __ATOMIC_RELAXED);
      break;
    case RESUME:
      __atomic_store_n(&paused, false, __ATOMIC_RELAXED);
      break;
    case PAIR:
      pairing();
      break;
    case CLOSE:
      portENTER_CRITICAL(&mux);
      pair_expires = 0;
      portEXIT_CRITICAL(&mux);
      break;
    case CONFIG:
      break;
    }
  }
  if (revision != config_revision()) {
    config_get(&cfg);
    revision = config_revision();
    cards_filter();
    reset_view();
    courier_set_theme(THEME_FIGURE(cfg.theme));
  }
  if (net_setup_active() && !net_connected())
    return;
  /* Push-Nachrichten haben Vorrang - auch in der Pause und im Eulen-Modus */
  if (!noting && notify_pop(&note))
    start_note();
  if (cfg.mode == 1)
    tty_tick();
  if (noting) {
    if (cfg.mode == 0) { /* Eule/Roboter bringen den Brief selbst */
      courier_tick();
      if (!courier_note_active())
        noting = false;
    } else if (++note_ticks >= note_hold)
      noting = false;
    return;
  }
  if (paused)
    return;
  if (cfg.mode == 0)
    courier_tick();
  else {
    if (!have_card) {
      /* weiterzaehlen, damit die Bootzeilen sich eintippen */
      unsigned keep = ticks;
      next_card();
      if (!have_card)
        ticks = keep + 1;
    }
    if (have_card && ++ticks >= hold) {
      ticks = 0;
      next_card();
    }
  }
}
static void setup(void) {
  char code[7], ip[32], pass[9];
  screen_pair_code(code);
  net_address(ip, sizeof ip);
  bool ap = net_setup_active() && !net_connected();
  gfx_fill(0, 0, GW, GH, RGB(246, 245, 234));
  gfx_text_plain(3, 2, ap ? "NEWSBOARD EINRICHTEN" : "NEWSBOARD VERBINDEN",
                 RGB(20, 65, 45));
  if (ap) {
    int n = qrcodegen_getSize(qr);
    gfx_fill(1, 12, n + 8, n + 8, RGB(255, 255, 255));
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        if (qrcodegen_getModule(qr, x, y))
          gfx_px(5 + x, 16 + y, RGB(0, 0, 0));
    gfx_text_plain(51, 14, "WLAN", RGB(70, 90, 80));
    gfx_text_plain(51, 21, net_ap_name(), RGB(20, 40, 30));
    gfx_text_plain(51, 32, "PASSWORT:", RGB(70, 90, 80));
    memcpy(pass, net_ap_password(), 8);
    pass[8] = 0;
    gfx_text_plain(51, 39, pass, RGB(20, 40, 30));
    gfx_text_plain(51, 46, net_ap_password() + 8, RGB(20, 40, 30));
    gfx_text_plain(3, 61, "BROWSER: 192.168.4.1", RGB(20, 40, 30));
  } else {
    gfx_text_plain(4, 16, "IM GLEICHEN WLAN OEFFNEN:", RGB(40, 70, 55));
    gfx_text_plain(4, 27, "HTTP://", RGB(40, 70, 55));
    gfx_text_plain(4, 35, ip, RGB(20, 40, 30));
    gfx_text_plain(4, 48, "CODE IM BROWSER EINGEBEN", RGB(40, 70, 55));
  }
  char b[40];
  snprintf(b, sizeof b, "CODE: %s", code[0] ? code : "IM BROWSER ANFORDERN");
  gfx_text_plain(3, 71, b, RGB(20, 65, 45));
}
static void dashboard(void) {
  tty_draw(have_card ? &card : NULL, (const char(*)[26])lines, line_count,
           page, ticks, have_card ? hold : cfg.duration * 30, paused);
}
void screen_render(void) {
  char code[7];
  screen_pair_code(code);
  bool show = (net_setup_active() && !net_connected()) || code[0];
  lcd_frame_begin();
  if (!show && noting && cfg.mode == 1)
    for (int y = 0; y < GH; y += GFX_BAND) {
      gfx_band_begin(y);
      tty_draw_notify(&note_card, (const char(*)[26])note_lines, note_count,
                      note_ticks, note_hold, &note);
      gfx_band_end();
    }
  else if (!show && cfg.mode == 0)
    courier_render();
  else
    for (int y = 0; y < GH; y += GFX_BAND) {
      gfx_band_begin(y);
      if (show)
        setup();
      else
        dashboard();
      gfx_band_end();
    }
  lcd_wait(0);
  int h = 12;
  if (net_time_valid()) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    h = tm.tm_hour;
  }
  bool night = cfg.night_start < cfg.night_end
                   ? (h >= cfg.night_start && h < cfg.night_end)
                   : cfg.night_start > cfg.night_end &&
                         (h >= cfg.night_start || h < cfg.night_end);
  lcd_backlight(show ? 80 : night ? cfg.night_brightness : cfg.brightness);
}
