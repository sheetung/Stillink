#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
void online_ota_register(httpd_handle_t server);
bool online_ota_busy(void);
