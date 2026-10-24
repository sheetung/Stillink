#pragma once
#include "esp_http_server.h"
#include "esp_http_client.h"
void service_access_init(void);
void service_access_reset(void);
void service_access_register(httpd_handle_t server);
void service_access_attach(esp_http_client_handle_t client, const char *url);

void service_access_base(char *out, size_t size);
