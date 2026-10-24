#include "display_scheduler.h"
#include "dates.h"
#include "connection_card.h"
#include "service_access.h"
#include "auth.h"
#include "music.h"
#include "wifi_lan.h"
#include "frame.h"
#include "power.h"
#include "online_ota.h"
#include "weather.h"
#include "album.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct { bool enabled; char url[192]; } config_t;
static config_t s_config;
static SemaphoreHandle_t s_lock;
static unsigned s_revision;
static bool s_loading, s_force;
static char s_error[80], s_version[65];
static TickType_t s_last_refresh;

bool music_enabled(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_config.enabled;
    xSemaphoreGive(s_lock);
    return enabled;
}

esp_err_t music_disable(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (s_config.enabled) {
        config_t next = s_config;
        next.enabled = false;
        nvs_handle_t nvs;
        err = nvs_open("music", NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            err = nvs_set_blob(nvs, "config_v1", &next, sizeof(next));
            if (err == ESP_OK) err = nvs_commit(nvs);
            nvs_close(nvs);
        }
        if (err == ESP_OK) {
            s_config = next;
            ++s_revision;
            s_force = false;
            s_error[0] = 0;
        }
    }
    xSemaphoreGive(s_lock);
    if(err==ESP_OK)display_scheduler_changed();
    return err;
}

void music_factory_reset(void)
{
    nvs_handle_t nvs;
    if (nvs_open("music", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs); nvs_commit(nvs); nvs_close(nvs);
    }
}

static void headers(httpd_req_t *req)
{
    power_activity();
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
}
static esp_err_t get_handler(httpd_req_t *req)
{
    headers(req);
    cJSON *json = cJSON_CreateObject();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON_AddBoolToObject(json, "enabled", s_config.enabled);
    cJSON_AddStringToObject(json, "service_url", s_config.url);
    cJSON_AddStringToObject(json, "version", s_version);
    cJSON_AddStringToObject(json, "error", s_error);
    cJSON_AddStringToObject(json, "status", !s_config.enabled ? "disabled" : !wifi_lan_connected() ? "waiting_wifi" :
        s_loading ? "loading" : s_error[0] ? "error" : s_version[0] ? "ready" : "waiting");
    xSemaphoreGive(s_lock);
    char *body = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    esp_err_t err = httpd_resp_sendstr(req, body); free(body); return err;
}
static bool valid_url(const char *url)
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
    if (req->content_len < 2 || req->content_len > 512) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    char body[513]; int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
        got += n;
    }
    body[got] = 0;
    cJSON *json = cJSON_Parse(body), *enabled = cJSON_GetObjectItemCaseSensitive(json, "enabled"),
        *url = cJSON_GetObjectItemCaseSensitive(json, "service_url");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    config_t next = s_config;
    bool valid = cJSON_IsBool(enabled);
    if (valid) next.enabled = cJSON_IsTrue(enabled);
    if (url) {
        valid = valid && cJSON_IsString(url) && strlen(url->valuestring) < sizeof(next.url) &&
            (!url->valuestring[0] || valid_url(url->valuestring));
        if (valid) {
            strlcpy(next.url, url->valuestring, sizeof(next.url));
            size_t n = strlen(next.url); while (n && next.url[n-1] == '/') next.url[--n] = 0;
        }
    }
    if (next.enabled && !next.url[0]) valid = false;
    cJSON_Delete(json);
    xSemaphoreGive(s_lock);
    esp_err_t err = ESP_OK;
    if (valid && next.enabled) err = dates_disable();
    if (valid && next.enabled && err == ESP_OK) err = weather_disable();
    if (valid && next.enabled && err == ESP_OK) err = album_disable();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (valid && err == ESP_OK) {
        nvs_handle_t nvs;
        err = nvs_open("music", NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            err = nvs_set_blob(nvs, "config_v1", &next, sizeof(next));
            if (err == ESP_OK) err = nvs_commit(nvs);
            nvs_close(nvs);
        }
        if (err == ESP_OK) {
            if (strcmp(next.url, s_config.url)) s_version[0] = 0;
            s_config = next; ++s_revision; s_force = next.enabled; s_error[0] = 0;
        }
    }
    xSemaphoreGive(s_lock);
    if (!valid) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid music settings");
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save music settings");
    connection_card_release();
    display_scheduler_changed();
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}
static esp_err_t refresh_handler(httpd_req_t *req)
{
    headers(req);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_config.enabled && wifi_lan_connected();
    if (ready) s_force = true;
    xSemaphoreGive(s_lock);
    display_scheduler_changed();
    httpd_resp_set_status(req, ready ? "202 Accepted" : "409 Conflict");
    return httpd_resp_sendstr(req, ready ? "{\"status\":\"queued\"}" : "{\"error\":\"Enable music and connect Wi-Fi first\"}");
}
static bool still_current(void *context)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = s_config.enabled && s_revision == *(unsigned *)context && display_scheduler_current();
    xSemaphoreGive(s_lock); return valid;
}
static esp_http_client_handle_t open_request(const char *base, const char *path)
{
    char url[320]; snprintf(url, sizeof(url), "%s%s", base, path);
    esp_http_client_config_t opts = {.url=url, .timeout_ms=10000, .buffer_size=1024,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t client = esp_http_client_init(&opts);
    if (!client) return NULL;
    if ((service_access_attach(client, url), esp_http_client_open(client, 0)) != ESP_OK || esp_http_client_fetch_headers(client) < 0 ||
        esp_http_client_get_status_code(client) != 200) {
        esp_http_client_close(client); esp_http_client_cleanup(client); return NULL;
    }
    return client;
}
display_result_t music_run(void)
{
        xSemaphoreTake(s_lock, portMAX_DELAY);
        config_t config = s_config; unsigned revision = s_revision;
        xSemaphoreGive(s_lock);
        if (!config.enabled || !wifi_lan_connected() || (strncmp(config.url,"https://",8)==0 && time(NULL)<1700000000)) return display_result(30000,NULL);
        char error[80] = "", version[65] = "";
        bool playing = false, ok = false;
        esp_http_client_handle_t client = open_request(config.url, "/v1/music.json");
        if (client) {
            char body[2049]; int used = 0, n;
            while (used < 2048 && (n = esp_http_client_read(client, body+used, 2048-used)) > 0) used += n;
            body[used] = 0;
            cJSON *json = cJSON_Parse(body), *v = cJSON_GetObjectItemCaseSensitive(json,"version"), *p = cJSON_GetObjectItemCaseSensitive(json,"playing");
            ok = cJSON_IsString(v) && strlen(v->valuestring)<sizeof(version) && cJSON_IsBool(p);
            if (ok) { strlcpy(version,v->valuestring,sizeof(version)); playing=cJSON_IsTrue(p); }
            cJSON_Delete(json); esp_http_client_close(client); esp_http_client_cleanup(client);
        }
        if (!ok) strlcpy(error,"Music service unavailable",sizeof(error));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool current = s_revision == revision && s_config.enabled;
        bool due = current && ok && playing && version[0] && (s_force ||
            (strcmp(version,s_version) && (!s_version[0] || (xTaskGetTickCount()-s_last_refresh)>=pdMS_TO_TICKS(60000))));
        if (due) { s_loading=true; s_force=false; }
        xSemaphoreGive(s_lock);
        if (due) {
            char path[96]; snprintf(path,sizeof(path),"/v1/music.bin?v=%s",version);
            client = open_request(config.url,path);
            ok = client && frame_download_display(client,still_current,&revision)==0;
            if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
            if (!ok) strlcpy(error,"Music frame download or refresh failed",sizeof(error));
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_loading = false;
        if (s_revision==revision && s_config.enabled) {
            strlcpy(s_error,error,sizeof(s_error));
            if (due && ok) { strlcpy(s_version,version,sizeof(s_version)); s_last_refresh=xTaskGetTickCount(); }
            if (due && !ok) s_force = true;
        }
        xSemaphoreGive(s_lock);
        return display_result(error[0]?30000:5000,error[0]?error:NULL);
}
void music_init(void)
{
    s_lock=xSemaphoreCreateMutex(); ESP_ERROR_CHECK(s_lock ? ESP_OK : ESP_ERR_NO_MEM);
    nvs_handle_t nvs; size_t size=sizeof(s_config);
    if (nvs_open("music",NVS_READONLY,&nvs)==ESP_OK) {
        if (nvs_get_blob(nvs,"config_v1",&s_config,&size)!=ESP_OK || size!=sizeof(s_config)) memset(&s_config,0,sizeof(s_config));
        nvs_close(nvs);
    }

}
void music_register(httpd_handle_t server)
{
    const httpd_uri_t routes[]={
        {.uri="/api/music",.method=HTTP_GET,.handler=get_handler},
        {.uri="/api/music",.method=HTTP_POST,.handler=config_handler},
        {.uri="/api/music/refresh",.method=HTTP_POST,.handler=refresh_handler},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i) ESP_ERROR_CHECK(auth_register_uri_handler(server,&routes[i]));
}
