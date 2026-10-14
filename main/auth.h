#pragma once
#include "esp_http_server.h"
#include <stdbool.h>
void auth_init(void);
void auth_factory_reset(void);
bool auth_first_boot(void);
void auth_key_copy(char *out, size_t size);
esp_err_t auth_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route);
void auth_register(httpd_handle_t server);
