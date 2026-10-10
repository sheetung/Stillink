#include "wifi_lan.h"
#include "power.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define CONNECTED BIT0
#define DISCONNECTED BIT1
#define CONNECTING BIT2
#define FAILED BIT3
#define STARTED BIT4
#define SCAN_MAX 16
#define STORAGE "wifi_lan"

typedef struct { char ssid[33]; char password[65]; } credentials_t;
static const char *TAG = "wifi_lan";
static EventGroupHandle_t s_events;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_radio;
static esp_netif_t *s_sta;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_ssid[33];
static bool s_saved;
static int s_reason;

static esp_err_t store_credentials(const credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    if (credentials) {
        err = nvs_set_str(handle, "ssid", credentials->ssid);
        if (err == ESP_OK) err = nvs_set_str(handle, "password", credentials->password);
    } else {
        err = nvs_erase_all(handle);
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static bool load_credentials(credentials_t *credentials)
{
    nvs_handle_t handle;
    if (nvs_open(STORAGE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t ssid_size = sizeof(credentials->ssid), password_size = sizeof(credentials->password);
    esp_err_t err = nvs_get_str(handle, "ssid", credentials->ssid, &ssid_size);
    if (err == ESP_OK) err = nvs_get_str(handle, "password", credentials->password, &password_size);
    nvs_close(handle);
    return err == ESP_OK && credentials->ssid[0];
}

static void station_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_events, STARTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        xEventGroupClearBits(s_events, DISCONNECTED);
        xEventGroupSetBits(s_events, CONNECTED);
        ESP_LOGI(TAG, "LAN ready: http://" IPSTR, IP2STR(&event->ip_info.ip));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        portENTER_CRITICAL(&s_lock);
        s_reason = event->reason;
        portEXIT_CRITICAL(&s_lock);
        xEventGroupClearBits(s_events, CONNECTED);
        xEventGroupSetBits(s_events, DISCONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(s_events, CONNECTED);
        xEventGroupSetBits(s_events, DISCONNECTED);
    }
}

static bool connect_station(const credentials_t *credentials)
{
    esp_wifi_disconnect();
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, strlen(credentials->ssid));
    memcpy(config.sta.password, credentials->password, strlen(credentials->password));
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) return false;
    for (int attempt = 0; attempt < 3 && uxQueueMessagesWaiting(s_queue) == 0; ++attempt) {
        xEventGroupClearBits(s_events, CONNECTED | DISCONNECTED);
        if (esp_wifi_connect() != ESP_OK) return false;
        for (int interval = 0; interval < 100 && uxQueueMessagesWaiting(s_queue) == 0; ++interval) {
            EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED | DISCONNECTED,
                                                 pdFALSE, pdFALSE, pdMS_TO_TICKS(100));
            if (bits & CONNECTED) return true;
            if (bits & DISCONNECTED) break;
        }
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

static void connection_task(void *arg)
{
    (void)arg;
    xEventGroupWaitBits(s_events, STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    credentials_t credentials = {0};
    bool have_credentials = load_credentials(&credentials);
    bool new_credentials = false;
    portENTER_CRITICAL(&s_lock);
    s_saved = have_credentials;
    portEXIT_CRITICAL(&s_lock);
    for (;;) {
        if (xQueueReceive(s_queue, &credentials, have_credentials ? 0 : pdMS_TO_TICKS(30000))) {
            have_credentials = credentials.ssid[0] != '\0';
            new_credentials = have_credentials;
            xSemaphoreTake(s_radio, portMAX_DELAY);
            esp_wifi_disconnect();
            xSemaphoreGive(s_radio);
            xEventGroupClearBits(s_events, CONNECTED | CONNECTING | FAILED);
        }
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_ssid, credentials.ssid, sizeof(s_ssid));
        portEXIT_CRITICAL(&s_lock);
        if (!have_credentials) continue;
        xEventGroupClearBits(s_events, FAILED);
        xEventGroupSetBits(s_events, CONNECTING);
        xSemaphoreTake(s_radio, portMAX_DELAY);
        bool connected = connect_station(&credentials);
        xSemaphoreGive(s_radio);
        if (uxQueueMessagesWaiting(s_queue)) continue;
        if (connected && new_credentials) {
            xSemaphoreTake(s_radio, portMAX_DELAY);
            if (uxQueueMessagesWaiting(s_queue)) { xSemaphoreGive(s_radio); continue; }
            esp_err_t err = store_credentials(&credentials);
            xSemaphoreGive(s_radio);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "save Wi-Fi credentials failed: %s", esp_err_to_name(err));
                esp_wifi_disconnect();
                connected = false;
            } else {
                new_credentials = false;
                portENTER_CRITICAL(&s_lock);
                s_saved = true;
                portEXIT_CRITICAL(&s_lock);
            }
        }
        xEventGroupClearBits(s_events, CONNECTING);
        if (connected) {
            while ((xEventGroupGetBits(s_events) & CONNECTED) && !uxQueueMessagesWaiting(s_queue))
                vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            xEventGroupSetBits(s_events, FAILED);
            /* Saved networks reconnect after outages; a rejected new network waits for input. */
            if (new_credentials) have_credentials = false;
            for (int i = 0; i < 300 && !uxQueueMessagesWaiting(s_queue); ++i)
                vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void wifi_lan_init(void)
{
    s_events = xEventGroupCreate();
    s_queue = xQueueCreate(1, sizeof(credentials_t));
    s_radio = xSemaphoreCreateMutex();
    s_sta = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(s_events && s_queue && s_radio && s_sta ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, station_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, station_event, NULL));
    ESP_ERROR_CHECK(xTaskCreate(connection_task, "wifi_lan", 4096, NULL, 4, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}

esp_err_t wifi_lan_connect(const char *ssid, const char *password)
{
    size_t ssid_len = strlen(ssid), pass_len = strlen(password);
    if (!ssid_len || ssid_len > 32 || pass_len > 64 || (pass_len && pass_len < 8))
        return ESP_ERR_INVALID_ARG;
    if (pass_len == 64) {
        for (size_t i = 0; i < pass_len; ++i)
            if (!isxdigit((unsigned char)password[i])) return ESP_ERR_INVALID_ARG;
    }
    credentials_t credentials = {0};
    strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
    strlcpy(credentials.password, password, sizeof(credentials.password));
    xEventGroupClearBits(s_events, FAILED);
    xEventGroupSetBits(s_events, CONNECTING);
    return xQueueOverwrite(s_queue, &credentials) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_lan_forget(void)
{
    credentials_t credentials = {0};
    if (s_queue) xQueueOverwrite(s_queue, &credentials);
    if (s_radio) xSemaphoreTake(s_radio, portMAX_DELAY);
    esp_err_t err = store_credentials(NULL);
    if (s_radio) xSemaphoreGive(s_radio);
    if (err != ESP_OK) return err;
    portENTER_CRITICAL(&s_lock);
    s_saved = false;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

bool wifi_lan_keep_awake(void)
{
    return s_events && (xEventGroupGetBits(s_events) & (CONNECTED | CONNECTING));
}

/* SSIDs may contain quotes/control bytes; escape before inserting into JSON. */
static void json_escape(char *out, const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') { *out++ = '\\'; *out++ = *p; }
        else if (*p < 0x20) { sprintf(out, "\\u%04x", *p); out += 6; }
        else *out++ = *p;
    }
    *out = '\0';
}

static esp_err_t status_handler(httpd_req_t *req)
{
    power_activity();
    EventBits_t bits = xEventGroupGetBits(s_events);
    char ssid[33], escaped[193], ip[16] = "";
    bool saved;
    int reason;
    portENTER_CRITICAL(&s_lock);
    strlcpy(ssid, s_ssid, sizeof(ssid));
    saved = s_saved;
    reason = s_reason;
    portEXIT_CRITICAL(&s_lock);
    const char *status = bits & CONNECTING ? "connecting" : bits & FAILED ? "failed" :
                         bits & CONNECTED ? "connected" : "idle";
    if (bits & CONNECTED) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(s_sta, &info) == ESP_OK)
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    }
    json_escape(escaped, ssid);
    char response[384];
    snprintf(response, sizeof(response),
             "{\"status\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"saved\":%s,\"reason\":%d}",
             status, escaped, ip, saved ? "true" : "false", reason);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    power_activity();
    if ((xEventGroupGetBits(s_events) & CONNECTING) || xSemaphoreTake(s_radio, 0) != pdTRUE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Wi-Fi is connecting; retry later");
    }
    wifi_ap_record_t *records = calloc(SCAN_MAX, sizeof(*records));
    uint16_t count = SCAN_MAX;
    wifi_scan_config_t config = {0};
    config.scan_time.active.min = 50;
    config.scan_time.active.max = 120;
    esp_err_t err = records ? esp_wifi_scan_start(&config, true) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) esp_wifi_clear_ap_list();
    xSemaphoreGive(s_radio);
    power_activity();
    if (err != ESP_OK) {
        free(records);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed");
    }
    char *response = calloc(1, SCAN_MAX * 256 + 32);
    if (!response) { free(records); return ESP_ERR_NO_MEM; }
    size_t offset = (size_t)sprintf(response, "{\"networks\":[");
    bool first = true;
    for (uint16_t i = 0; i < count; ++i) {
        char ssid[33], escaped[193];
        memcpy(ssid, records[i].ssid, 32);
        ssid[32] = '\0';
        if (!ssid[0]) continue;
        json_escape(escaped, ssid);
        offset += (size_t)sprintf(response + offset,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secured\":%s}", first ? "" : ",", escaped,
            records[i].rssi, records[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
        first = false;
    }
    strcpy(response + offset, "]}");
    free(records);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    err = httpd_resp_sendstr(req, response);
    free(response);
    return err;
}

void wifi_lan_register(httpd_handle_t server)
{
    const httpd_uri_t status = { .uri = "/api/wifi/status", .method = HTTP_GET, .handler = status_handler };
    const httpd_uri_t scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = scan_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &scan));
}
