#ifndef EPAPER_WIFI_LAN_H
#define EPAPER_WIFI_LAN_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

/* Starts with AP+STA; disables the AP after LAN connection and restores it on loss. */
void wifi_lan_init(void);
esp_err_t wifi_lan_connect(const char *ssid, const char *password);
esp_err_t wifi_lan_forget(void);
bool wifi_lan_keep_awake(void);
bool wifi_lan_connected(void);
void wifi_lan_register(httpd_handle_t server);

#endif
