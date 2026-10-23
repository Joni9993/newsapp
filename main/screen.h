#pragma once
#include <stdbool.h>
void screen_init(void);
void screen_tick(void);
void screen_render(void);
bool screen_command(const char *command);
bool screen_paused(void);
void screen_pair_code(char out[7]);
void screen_show_pair(void);
