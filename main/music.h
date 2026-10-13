#ifndef STILLINK_MUSIC_H
#define STILLINK_MUSIC_H
#include <stdbool.h>
#include "esp_http_server.h"
void music_init(void);
void music_register(httpd_handle_t server);
void music_factory_reset(void);
bool music_enabled(void);
esp_err_t music_disable(void);
#endif
