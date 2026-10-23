#pragma once
#include "cJSON.h"
void calendar_fetch(void);
void calendar_request_check(int slot);
void calendar_process_check(void);
cJSON *calendar_status(void);
