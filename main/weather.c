#include "weather.h"
#include "music.h"
#include "wifi_lan.h"
#include "frame.h"
#include "power.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "online_ota.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "weather";
static weather_config_t s_config = {.minutes = {480, 720, 1080}};
static weather_data_t s_data;
static SemaphoreHandle_t s_lock;
static unsigned s_revision;
static bool s_requested, s_has_data, s_loading;
static char s_error[80];
static int64_t s_last_slot = -1;
static TickType_t s_retry;

static cJSON *item(cJSON *json, const char *key) { return cJSON_GetObjectItemCaseSensitive(json, key); }
static bool number(cJSON *value) { return cJSON_IsNumber(value) && isfinite(value->valuedouble); }
static void headers(httpd_req_t *req)
{
    power_activity();
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
}

static esp_err_t save_config(const weather_config_t *config)
{

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("weather", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "config_v3", config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "save weather: %s", esp_err_to_name(err));
    return err;
}

void weather_factory_reset(void)
{
    nvs_handle_t nvs;
    if (nvs_open("weather", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static esp_err_t get_handler(httpd_req_t *req)
{
    headers(req);
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddBoolToObject(json, "enabled", s_config.enabled);
    cJSON_AddStringToObject(json, "name", s_config.name);
    cJSON_AddStringToObject(json, "service_url", s_config.service_url);
    cJSON_AddNumberToObject(json, "latitude", s_config.latitude);
    cJSON_AddNumberToObject(json, "longitude", s_config.longitude);
    cJSON *times = cJSON_AddArrayToObject(json, "times");
    for (int i = 0; i < 3; ++i) cJSON_AddItemToArray(times, cJSON_CreateNumber(s_config.minutes[i]));
    const char *state = !s_config.enabled ? "disabled" : !wifi_lan_connected() ? "waiting_wifi" :
                        s_loading ? "loading" : time(NULL) < 1700000000 ? "waiting_time" : s_error[0] ? "error" : s_has_data ? "ready" : "waiting";
    cJSON_AddStringToObject(json, "status", state);
    cJSON_AddStringToObject(json, "error", s_error);
    cJSON_AddStringToObject(json, "source", "Open-Meteo");
    if (s_has_data) {
        cJSON *data = cJSON_AddObjectToObject(json, "data");
        cJSON_AddNumberToObject(data, "updated", s_data.updated);
        cJSON_AddNumberToObject(data, "utc_offset", s_data.utc_offset);
    }
    xSemaphoreGive(s_lock);
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

static bool service_url_ok(const char *url)
{
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) return false;
    const char *host = strstr(url, "://") + 3;
    if (!*host || *host == '/') return false;
    for (const char *p = url; *p; ++p) if ((unsigned char)*p <= 32 || *p == '?' || *p == '#' || *p == '@') return false;
    return true;
}

static esp_err_t config_handler(httpd_req_t *req)
{
    headers(req);
    if (req->content_len < 2 || req->content_len > 1024)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    char *body = calloc(1, req->content_len + 1);
    weather_config_t *next = malloc(sizeof(*next));
    if (!body || !next) { free(body); free(next); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory"); }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) { free(body); free(next); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body"); }
        received += n;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *next = s_config;
    xSemaphoreGive(s_lock);
    cJSON *enabled = item(json, "enabled"), *name = item(json, "name");
    cJSON *lat = item(json, "latitude"), *lon = item(json, "longitude"), *times = item(json, "times");
    cJSON *service = item(json, "service_url");
    bool valid = json && cJSON_IsBool(enabled);
    if (valid) next->enabled = cJSON_IsTrue(enabled);
    /* A switch-only update can disable without repeating location/service data. */
    if (name || lat || lon || times) {
        valid = valid && cJSON_IsString(name) && strlen(name->valuestring) > 0 && strlen(name->valuestring) < sizeof(next->name) &&
                number(lat) && fabs(lat->valuedouble) <= 90 && number(lon) && fabs(lon->valuedouble) <= 180 &&
                cJSON_IsArray(times) && cJSON_GetArraySize(times) == 3;
        if (valid) {
            strlcpy(next->name, name->valuestring, sizeof(next->name));
            next->latitude = lat->valuedouble;
            next->longitude = lon->valuedouble;
            for (int i = 0; i < 3; ++i) {
                cJSON *t = cJSON_GetArrayItem(times, i);
                if (!number(t) || t->valuedouble != t->valueint || t->valueint < 0 || t->valueint >= 1440) { valid = false; break; }
                next->minutes[i] = t->valueint;
            }
            if (!(next->minutes[0] < next->minutes[1] && next->minutes[1] < next->minutes[2])) valid = false;
        }
    }
    if (service) {
        valid = valid && cJSON_IsString(service) && strlen(service->valuestring) < sizeof(next->service_url) &&
                (!service->valuestring[0] || service_url_ok(service->valuestring));
        if (valid) {
            strlcpy(next->service_url, service->valuestring, sizeof(next->service_url));
            size_t n = strlen(next->service_url);
            while (n && next->service_url[n - 1] == '/') next->service_url[--n] = 0;
        }
    }
    if (next->enabled && (!next->name[0] || !next->service_url[0])) valid = false;
    cJSON_Delete(json);
    if (!valid) { free(next); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid weather settings"); }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = save_config(next);
    if (err == ESP_OK) {
        bool location_changed = strcmp(s_config.name, next->name) || s_config.latitude != next->latitude || s_config.longitude != next->longitude || strcmp(s_config.service_url, next->service_url);
        s_config = *next;
        ++s_revision;
        s_requested = next->enabled;
        if (location_changed) s_has_data = false;
        s_error[0] = 0;
        s_retry = 0;
    }
    xSemaphoreGive(s_lock);
    free(next);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save weather settings");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t refresh_handler(httpd_req_t *req)
{
    headers(req);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool available = s_config.enabled && wifi_lan_connected();
    if (available) { s_requested = true; s_retry = 0; }
    xSemaphoreGive(s_lock);
    if (!available) { httpd_resp_set_status(req, "409 Conflict"); return httpd_resp_sendstr(req, "{\"error\":\"Enable weather and connect Wi-Fi first\"}"); }
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "{\"status\":\"queued\"}");
}

typedef struct { weather_data_t *data; } response_t;
static esp_err_t receive_event(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (!strcasecmp(event->header_key, "X-Weather-UTC-Offset")) r->data->utc_offset = atoi(event->header_value);
        if (!strcasecmp(event->header_key, "X-Weather-Updated")) r->data->updated = strtoll(event->header_value, NULL, 10);
    }
    return ESP_OK;
}
static bool still_current(void *context)
{
    unsigned revision = *(unsigned *)context;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool current = s_config.enabled && s_revision == revision;
    xSemaphoreGive(s_lock);
    return current;
}
static bool fetch_weather(const weather_config_t *config, weather_data_t *data, char *error, unsigned revision)
{
    char encoded[sizeof(config->name) * 3], url[640];
    char *out = encoded;
    for (const unsigned char *p = (const unsigned char *)config->name; *p; ++p) {
        sprintf(out, "%%%02X", *p); out += 3;
    }
    *out = 0;
    snprintf(url, sizeof(url), "%s/v1/weather.bin?latitude=%.5f&longitude=%.5f&name=%s", config->service_url, config->latitude, config->longitude, encoded);
    response_t response = {.data = data};
    esp_http_client_config_t options = {.url = url, .timeout_ms = 30000, .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = receive_event, .user_data = &response, .buffer_size = 2048, .user_agent = "Stillink/weather"};
    esp_http_client_handle_t client = esp_http_client_init(&options);
    esp_err_t err = client ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
    int result = -2, status = 0;
    if (err == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
        if (status == 200 && data->updated >= 1700000000 && abs(data->utc_offset) <= 86400)
            result = frame_download_display(client, still_current, &revision);
    }
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    if (result == 0) return true;
    if (err != ESP_OK) snprintf(error, 80, "Network request: %s", esp_err_to_name(err));
    else if (status != 200) snprintf(error, 80, "Weather service HTTP %d", status);
    else strlcpy(error, result == -1 ? "Screen busy or refresh failed" : "Invalid or incomplete weather frame", 80);
    return false;
}

static int64_t schedule_slot(time_t now, int offset, const weather_config_t *config)
{
    int64_t local = (int64_t)now + offset, day = local / 86400;
    int minute = (local % 86400) / 60;
    for (int i = 2; i >= 0; --i) if (minute >= config->minutes[i]) return day * 3 + i;
    return (day - 1) * 3 + 2;
}

static void worker(void *unused)
{
    weather_config_t *config = malloc(sizeof(*config));
    if (!config) { vTaskDelete(NULL); return; }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (online_ota_busy()) continue;
        time_t now = time(NULL);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int offset = s_has_data ? s_data.utc_offset : 8 * 3600;
        int64_t slot = schedule_slot(now, offset, &s_config);
        bool due = !s_has_data || s_requested || slot > s_last_slot;
        bool retry_ready = !s_retry || (int32_t)(xTaskGetTickCount() - s_retry) >= 0;
        /* NTP must establish time before certificate validity checks. */
        if (!s_config.enabled || music_enabled() || !wifi_lan_connected() || now < 1700000000 || !due || !retry_ready) { xSemaphoreGive(s_lock); continue; }
        *config = s_config;
        unsigned revision = s_revision;
        s_requested = false;
        s_loading = true;
        xSemaphoreGive(s_lock);
        weather_data_t data = {0};
        char error[80] = "";
        bool ok = fetch_weather(config, &data, error, revision);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool current = revision == s_revision && s_config.enabled;
        xSemaphoreGive(s_lock);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_loading = false;
        if (current && revision == s_revision) {
            if (ok) {
                s_data = data;
                s_has_data = true;
                s_last_slot = schedule_slot(time(NULL), data.utc_offset, config);
                s_retry = 0;
                s_error[0] = 0;
            } else {
                strlcpy(s_error, error, sizeof(s_error));
                s_requested = true;
                s_retry = xTaskGetTickCount() + pdMS_TO_TICKS(300000);
            }
        }
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "weather %s: %s", config->name, ok ? "updated" : error);
    }
}

void weather_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_lock ? ESP_OK : ESP_ERR_NO_MEM);
    nvs_handle_t nvs;
    if (nvs_open("weather", NVS_READWRITE, &nvs) == ESP_OK) {
        size_t size = sizeof(s_config);
        weather_config_t config = {0};
        if (nvs_get_blob(nvs, "config_v3", &config, &size) == ESP_OK && size == sizeof(config)) s_config = config;
        /* Remove unreleased device-font prototypes; no font blobs remain in NVS. */
        nvs_erase_key(nvs, "config_v1");
        nvs_erase_key(nvs, "config_v2");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
    ESP_ERROR_CHECK(xTaskCreate(worker, "weather", 8192, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void weather_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/api/weather", .method = HTTP_GET, .handler = get_handler},
        {.uri = "/api/weather", .method = HTTP_POST, .handler = config_handler},
        {.uri = "/api/weather/refresh", .method = HTTP_POST, .handler = refresh_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
}
