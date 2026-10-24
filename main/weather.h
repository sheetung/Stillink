#ifndef STILLINK_WEATHER_H
#define STILLINK_WEATHER_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"

typedef struct {
    uint8_t enabled;
    char name[96];
    double latitude, longitude;
    uint16_t minutes[3];
    char service_url[192];
} weather_config_t;
typedef struct {
    int utc_offset;
    int64_t updated;
} weather_data_t;

void weather_init(void);
void weather_register(httpd_handle_t server);
void weather_factory_reset(void);
esp_err_t weather_disable(void);
#endif

#include "display_scheduler.h"
display_result_t weather_run(void);
bool weather_enabled(void);
