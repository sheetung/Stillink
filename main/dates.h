#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
void dates_init(void);
void dates_register(httpd_handle_t server);
void dates_factory_reset(void);
esp_err_t dates_disable(void);
bool dates_enabled(void);

#include "display_scheduler.h"
display_result_t dates_run(void);
