#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "net.h"
#include "ota.h"
#include "nvs_flash.h"
#include "screen.h"
#include "sources.h"
bool web_start(void);
void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE("newsboard", "NVS initialization failed (%s); storage preserved",
             esp_err_to_name(err));
    return;
  }
  if (!config_init())
    ESP_LOGW("newsboard",
             "Invalid config: safe defaults loaded; other storage preserved");
  if (!lcd_init()) {
    ESP_LOGE("newsboard", "Display initialization failed");
    return;
  }
  net_start();
  sources_start();
  screen_init();
  /* Erst wenn der Webserver laeuft, gilt ein frisch aktualisiertes Image als
   * gut; sonst Rollback auf die alte Firmware beim naechsten Neustart. */
  if (web_start())
    ota_mark_valid();
  ota_auto_start();
  gpio_config_t g = {.pin_bit_mask = 1ULL << 9,
                     .mode = GPIO_MODE_INPUT,
                     .pull_up_en = GPIO_PULLUP_ENABLE};
  gpio_config(&g);
  int held = 0;
  int64_t log_at = 0;
  for (;;) {
    screen_tick();
    screen_render();
    if (!gpio_get_level(9)) {
      if (++held == 150) {
        net_recovery();
        screen_show_pair();
      }
    } else
      held = 0;
    int64_t now = esp_timer_get_time();
    if (now - log_at > 60000000) {
      ESP_LOGI("newsboard", "uptime=%lld heap=%lu minimum=%lu wifi=%d",
               now / 1000000, (unsigned long)esp_get_free_heap_size(),
               (unsigned long)esp_get_minimum_free_heap_size(),
               net_connected());
      log_at = now;
    }
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}
