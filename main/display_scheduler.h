#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_http_server.h"
#define DISPLAY_WAIT_FOREVER UINT32_MAX
typedef struct { uint32_t next_ms; char error[80]; } display_result_t;
static inline display_result_t display_result(uint32_t ms,const char *error){display_result_t r={.next_ms=ms};if(error)snprintf(r.error,sizeof(r.error),"%s",error);return r;}
void display_scheduler_init(void);
void display_scheduler_register(httpd_handle_t server);
void display_scheduler_changed(void);
void display_scheduler_wake(void);
bool display_scheduler_current(void);
uint32_t display_scheduler_manual_ready(void);
uint32_t display_scheduler_clear(int cycles);
void display_scheduler_connection(void);
void display_scheduler_reset_notice(void);
