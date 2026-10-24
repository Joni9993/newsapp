if(NEWSBOARD_DISPLAY_TEST)
idf_component_register(SRCS "display_test.c" "lcd.c" "gfx.c" INCLUDE_DIRS "."
 REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc)
else()
idf_component_register(
    SRCS "main.c" "lcd.c" "gfx.c" "sprites.c" "owl_art.c" "json.c" "rss.c" "text.c" "cards.c" "net.c" "sources.c"
         "scene.c" "courier.c" "config.c" "modules.c" "ical.c" "calendar.c" "web.c" "ota.c" "screen.c" "tty.c" "notify.c" "automate_flow.c" "vendor/qrcodegen.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_usb_serial_jtag esp_timer
             esp_wifi esp_netif esp_event esp_http_client esp_http_server json mbedtls nvs_flash
             app_update esp_https_ota esp_app_format esp_system
    EMBED_TXTFILES "index.html" "app.js" "style.css")
endif()
if(NEWSBOARD_LEGACY_PANEL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LCD_LEGACY_PANEL=1)
endif()
