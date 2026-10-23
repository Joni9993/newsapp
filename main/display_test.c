#include "lcd.h"
#include "gfx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
void app_main(void) {
 if(!lcd_init())return;
 for(;;){lcd_frame_begin();for(int y=0;y<GH;y+=GFX_BAND){gfx_band_begin(y);gfx_fill(0,0,GW,GH,RGB(0,0,0));
 gfx_text(3,3,"NEWSBOARD - OBEN",RGB(255,255,255));
 gfx_fill(3,14,32,35,RGB(255,0,0));gfx_fill(37,14,32,35,RGB(0,255,0));gfx_fill(71,14,32,35,RGB(0,0,255));
 gfx_text(8,52,"ROT",RGB(255,255,255));gfx_text(43,52,"GRUEN",RGB(255,255,255));gfx_text(78,52,"BLAU",RGB(255,255,255));
 gfx_text(3,68,"ST7789 320 X 240",RGB(255,255,255));gfx_band_end();}lcd_wait(0);vTaskDelay(pdMS_TO_TICKS(1000));}
}
