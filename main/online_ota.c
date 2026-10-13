#include "online_ota.h"
#include "ota_web.h"
#include "version.h"
#include "settings.h"
#include "wifi_lan.h"
#include "power.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RELEASE_API "https://api.github.com/repos/sheetung/Stillink/releases/latest"
#define RELEASE_BASE "https://github.com/sheetung/Stillink/releases/download/"
#define JSON_LIMIT 16384
static SemaphoreHandle_t s_lock;
static bool s_busy, s_newer;
static char s_status[16] = "idle", s_error[128], s_tag[32], s_url[320];
static int s_size, s_received;
static char *s_notes;

bool online_ota_busy(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = s_busy;
    xSemaphoreGive(s_lock);
    return busy;
}

typedef struct { char *data; size_t used; } json_body_t;
static esp_err_t receive_json(esp_http_client_event_t *event)
{
    json_body_t *body = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && esp_http_client_get_status_code(event->client) == 200) {
        if (body->used + event->data_len >= JSON_LIMIT) return ESP_ERR_INVALID_SIZE;
        memcpy(body->data + body->used, event->data, event->data_len);
        body->used += event->data_len;
        body->data[body->used] = 0;
    }
    return ESP_OK;
}
static int version_number(const char *tag)
{
    int major, minor, patch, end = 0;
    if ((*tag == 'v' || *tag == 'V') && sscanf(tag + 1, "%d.%d.%d%n", &major, &minor, &patch, &end) == 3 &&
        !tag[end + 1] && major >= 0 && major < 1000 && minor >= 0 && minor < 1000 && patch >= 0 && patch < 1000)
        return major * 1000000 + minor * 1000 + patch;
    return -1;
}
static esp_err_t check_release(void)
{
    json_body_t body = {.data = calloc(1, JSON_LIMIT)};
    if (!body.data) return ESP_ERR_NO_MEM;
    esp_http_client_config_t config = {.url = RELEASE_API, .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach, .user_agent = "Stillink/" EPAPER_VERSION,
        .buffer_size = 1024, .event_handler = receive_json, .user_data = &body};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = client ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
    int status = client ? esp_http_client_get_status_code(client) : 0;
    if (client) esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_error, sizeof(s_error), "GitHub HTTP %d / %s", status, esp_err_to_name(err));
        xSemaphoreGive(s_lock);
        free(body.data); return ESP_FAIL;
    }
    cJSON *json = cJSON_Parse(body.data);
    free(body.data);
    cJSON *tag = cJSON_GetObjectItemCaseSensitive(json, "tag_name"), *asset;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    err = ESP_ERR_NOT_FOUND;
    if (cJSON_IsString(tag) && strlen(tag->valuestring) < sizeof(s_tag) && version_number(tag->valuestring) >= 0 && part) {
        char filename[96], expected[320];
        snprintf(filename, sizeof(filename), "stillink-%s-%s-ota.bin", tag->valuestring, CONFIG_IDF_TARGET);
        snprintf(expected, sizeof(expected), RELEASE_BASE "%s/%s", tag->valuestring, filename);
        cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(json, "assets")) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name"),
                *url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url"),
                *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
            if (cJSON_IsString(name) && !strcmp(name->valuestring, filename) && cJSON_IsString(url) &&
                !strcmp(url->valuestring, expected) && cJSON_IsNumber(size) && size->valuedouble > 0 && size->valuedouble <= part->size) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                strlcpy(s_tag, tag->valuestring, sizeof(s_tag));
                strlcpy(s_url, expected, sizeof(s_url));
                s_size = size->valueint;
                s_newer = version_number(s_tag) > version_number(EPAPER_VERSION);
                cJSON *notes = cJSON_GetObjectItemCaseSensitive(json, "body");
                free(s_notes);
                s_notes = cJSON_IsString(notes) ? strdup(notes->valuestring) : NULL;
                xSemaphoreGive(s_lock);
                err = ESP_OK; break;
            }
        }
    }
    cJSON_Delete(json);
    return err;
}

static esp_err_t install_release(void)
{
    esp_http_client_config_t http = {.url = s_url, .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach, .user_agent = "Stillink/" EPAPER_VERSION,
        .buffer_size = 4096, .buffer_size_tx = 1024, .keep_alive_enable = true};
    esp_https_ota_config_t config = {.http_config = &http};
    esp_https_ota_handle_t ota = NULL;
    esp_err_t err = esp_https_ota_begin(&config, &ota);
    if (err != ESP_OK) return err;
    esp_app_desc_t description;
    err = esp_https_ota_get_img_desc(ota, &description);
    if (err != ESP_OK || version_number(description.version) != version_number(s_tag)) {
        esp_https_ota_abort(ota);
        return err == ESP_OK ? ESP_ERR_INVALID_VERSION : err;
    }
    do {
        power_activity();
        if (!wifi_lan_connected()) { err = ESP_ERR_INVALID_STATE; break; }
        err = esp_https_ota_perform(ota);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_received = esp_https_ota_get_image_len_read(ota);
        xSemaphoreGive(s_lock);
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);
    if (err == ESP_OK && (!esp_https_ota_is_complete_data_received(ota) || s_received != s_size)) err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) { esp_https_ota_abort(ota); return err; }
    err = esp_https_ota_finish(ota); /* validates image and chip, then switches boot slot */
    if (err != ESP_OK) return err;
    ota_web_clear();
    settings_reset_for_upgrade();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_status, "rebooting", sizeof(s_status));
    xSemaphoreGive(s_lock);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}
static void worker(void *arg)
{
    bool install = arg != NULL;
    esp_err_t err = install ? install_release() : check_release();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_status, err == ESP_OK ? "ready" : "error", sizeof(s_status));
    if (err != ESP_OK && !s_error[0]) snprintf(s_error, sizeof(s_error), "%s", esp_err_to_name(err));
    s_busy = false;
    xSemaphoreGive(s_lock);
    power_activity();
    vTaskDelete(NULL);
}
static esp_err_t status_handler(httpd_req_t *req)
{
    power_activity();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    cJSON *json = cJSON_CreateObject();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddStringToObject(json, "current", EPAPER_VERSION);
    cJSON_AddStringToObject(json, "chip", CONFIG_IDF_TARGET);
    cJSON_AddBoolToObject(json, "connected", wifi_lan_connected());
    cJSON_AddStringToObject(json, "status", s_status);
    cJSON_AddStringToObject(json, "latest", s_tag);
    cJSON_AddStringToObject(json, "release_notes", s_notes ? s_notes : "");
    cJSON_AddBoolToObject(json, "update_available", s_newer && s_url[0]);
    cJSON_AddNumberToObject(json, "size", s_size);
    cJSON_AddNumberToObject(json, "received", s_received);
    cJSON_AddStringToObject(json, "error", s_error);
    xSemaphoreGive(s_lock);
    char *body = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    esp_err_t err = httpd_resp_sendstr(req, body); free(body); return err;
}
static esp_err_t start_handler(httpd_req_t *req)
{
    bool install = !strcmp(req->uri, "/api/ota/online/install");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = wifi_lan_connected() && !s_busy && (!install || (s_newer && s_url[0]));
    if (ready) {
        s_busy = true; s_received = 0; s_error[0] = 0;
        if (!install) { s_tag[0] = s_url[0] = 0; s_newer = false; s_size = 0; free(s_notes); s_notes = NULL; }
        strlcpy(s_status, install ? "downloading" : "checking", sizeof(s_status));
        if (xTaskCreate(worker, "online_ota", 6144, install ? (void *)1 : NULL, 4, NULL) != pdPASS) {
            s_busy = false; ready = false;
            strlcpy(s_status, "error", sizeof(s_status));
            strlcpy(s_error, "Not enough memory", sizeof(s_error));
        }
    }
    xSemaphoreGive(s_lock);
    httpd_resp_set_status(req, ready ? "202 Accepted" : "409 Conflict");
    return status_handler(req);
}
void online_ota_register(httpd_handle_t server)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    httpd_uri_t routes[] = {
        {.uri = "/api/ota/online", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/ota/online/check", .method = HTTP_POST, .handler = start_handler},
        {.uri = "/api/ota/online/install", .method = HTTP_POST, .handler = start_handler},
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
}
