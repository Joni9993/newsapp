#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
typedef bool (*net_data_cb)(const char *data,size_t n,void *ctx);
void net_start(void);
bool net_connected(void);
bool net_time_valid(void);
bool net_configured(void);
bool net_setup_active(void);
void net_recovery(void);
const char *net_ap_name(void);
const char *net_ap_password(void);
void net_address(char *out,size_t size);
bool net_connect(const char *ssid,const char *password);
cJSON *net_status(void);
cJSON *net_scan(void);
int net_get(const char *url,net_data_cb cb,void *ctx);
/* Wie net_get, aber mit eigener Grenze fuer Groesse und Dauer */
int net_get_ex(const char *url,net_data_cb cb,void *ctx,size_t max_bytes,int max_seconds);
