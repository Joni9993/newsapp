#include "lcd.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_lcd_panel_io_handle_t io;
static volatile int sent, done;
static bool first;

static bool on_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    done++;
    return false;
}

static void cmd(uint8_t c, const uint8_t *p, int n)
{
    esp_lcd_panel_io_tx_param(io, c, p, n);
}

bool lcd_init(void)
{
    gpio_config_t oc = { .pin_bit_mask = 1ULL << LCD_PIN_RST, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&oc);
    /* Hintergrundlicht ueber PWM, damit es nachts gedimmt werden kann */
    ledc_timer_config_t lt = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                               .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_channel_config_t lc = { .gpio_num = LCD_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                 .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0 };
    ledc_timer_config(&lt);
    ledc_channel_config(&lc);
    gpio_set_level(LCD_PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_SCK, .mosi_io_num = LCD_PIN_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * 24 * 2,
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    esp_lcd_panel_io_spi_config_t cfg = {
        .cs_gpio_num = LCD_PIN_CS, .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0, .pclk_hz = 40 * 1000 * 1000,   /* fliegende Draehte: 40 MHz */
        .trans_queue_depth = 4, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = on_done,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io) != ESP_OK)
        return false;

    cmd(0x11, NULL, 0); vTaskDelay(pdMS_TO_TICKS(120));      /* Sleep out */
    cmd(0x3A, (uint8_t[]){ 0x55 }, 1);                       /* RGB565 */
    cmd(0x36, (uint8_t[]){ LCD_MADCTL }, 1);                 /* quer, je nach Panel */
    cmd(LCD_INVERT, NULL, 0);                                /* Farbumkehr je nach Panel */
    cmd(0x2A, (uint8_t[]){ 0, 0, (LCD_W - 1) >> 8, (LCD_W - 1) & 0xFF }, 4);
    cmd(0x2B, (uint8_t[]){ 0, 0, (LCD_H - 1) >> 8, (LCD_H - 1) & 0xFF }, 4);
    cmd(0x13, NULL, 0);
    cmd(0x29, NULL, 0);
    lcd_backlight(100);
    return true;
}

void lcd_backlight(int percent)
{
    static int last = -1;
    if (percent == last) return;
    last = percent;
    /* 256 bei 8 Bit heisst "immer an" - 255 liesse jede Periode einen Punkt dunkel */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)percent * 256 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void lcd_frame_begin(void) { first = true; }

void lcd_push(const uint16_t *buf, int rows)
{
    /* 0x2C beginnt oben links im Fenster, 0x3C schreibt dort weiter, wo das
     * letzte Stueck aufgehoert hat - so muss das Fenster nie neu gesetzt werden. */
    sent++;
    esp_lcd_panel_io_tx_color(io, first ? 0x2C : 0x3C, buf, (size_t)LCD_W * rows * 2);
    first = false;
}

void lcd_wait(int pending)
{
    while (sent - done > pending) taskYIELD();
}
