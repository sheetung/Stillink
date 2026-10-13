#ifndef STILLINK_ALBUM_H
#define STILLINK_ALBUM_H
#include <stdbool.h>
#include "esp_http_server.h"
void album_init(void);
void album_register(httpd_handle_t server);
void album_factory_reset(void);
bool album_enabled(void);
esp_err_t album_disable(void);
#endif
