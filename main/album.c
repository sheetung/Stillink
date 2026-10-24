#include "display_scheduler.h"
#include "dates.h"
#include "connection_card.h"
#include "service_access.h"
#include "auth.h"
#include "album.h"
#include "wifi_lan.h"
#include "frame.h"
#include "power.h"
#include "online_ota.h"
#include "weather.h"
#include "music.h"
#include "epd_drv.h"
#include "settings.h"
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

typedef struct { bool enabled; char url[192]; char playlist[33]; uint32_t interval; bool random; } config_t;
static config_t s_config = {.interval=1800};
static SemaphoreHandle_t s_lock;
static unsigned s_revision;
static bool s_loading, s_force;
static char s_error[80], s_version[65];
static TickType_t s_last_refresh;

bool album_enabled(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_config.enabled;
    xSemaphoreGive(s_lock);
    return enabled;
}

esp_err_t album_disable(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (s_config.enabled) {
        config_t next = s_config;
        next.enabled = false;
        nvs_handle_t nvs;
        err = nvs_open("album", NVS_READWRITE, &nvs);
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

void album_factory_reset(void)
{
    nvs_handle_t nvs;
    if (nvs_open("album", NVS_READWRITE, &nvs) == ESP_OK) {
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
    cJSON_AddStringToObject(json, "current", s_version);
    cJSON_AddStringToObject(json, "playlist", s_config.playlist);
    cJSON_AddNumberToObject(json, "interval", s_config.interval);
    cJSON_AddStringToObject(json, "mode", s_config.random ? "random" : "sequence");
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
    if (req->content_len < 2 || req->content_len > 768) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    char body[769]; int got = 0;
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
    cJSON *playlist = cJSON_GetObjectItemCaseSensitive(json, "playlist"),
        *interval = cJSON_GetObjectItemCaseSensitive(json, "interval"),
        *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
    if (playlist) {
        valid = valid && cJSON_IsString(playlist) && strlen(playlist->valuestring) < sizeof(next.playlist);
        if (valid) {
            for (const char *p = playlist->valuestring; *p; ++p)
                if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) valid = false;
            if (valid) strlcpy(next.playlist, playlist->valuestring, sizeof(next.playlist));
        }
    }
    if (interval) {
        valid = valid && cJSON_IsNumber(interval) && interval->valuedouble == interval->valueint && interval->valueint >= 60 && interval->valueint <= 86400;
        if (valid) next.interval = interval->valueint;
    }
    if (mode) {
        valid = valid && cJSON_IsString(mode) && (!strcmp(mode->valuestring,"sequence") || !strcmp(mode->valuestring,"random"));
        if (valid) next.random = !strcmp(mode->valuestring,"random");
    }
    if (next.enabled && (!next.url[0] || strlen(next.playlist) != 32)) valid = false;
    cJSON_Delete(json);
    xSemaphoreGive(s_lock);
    esp_err_t err = ESP_OK;
    if (valid && next.enabled) { err = dates_disable(); if (err == ESP_OK) err = weather_disable(); if (err == ESP_OK) err = music_disable(); }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (valid && err == ESP_OK) {
        nvs_handle_t nvs;
        err = nvs_open("album", NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            err = nvs_set_blob(nvs, "config_v1", &next, sizeof(next));
            if (err == ESP_OK) err = nvs_commit(nvs);
            nvs_close(nvs);
        }
        if (err == ESP_OK) {
            if (strcmp(next.url, s_config.url) || strcmp(next.playlist, s_config.playlist)) s_version[0] = 0;
            s_config = next; ++s_revision; s_force = next.enabled; s_error[0] = 0;
        }
    }
    xSemaphoreGive(s_lock);
    if (!valid) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid album settings");
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save album settings");
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
    return httpd_resp_sendstr(req, ready ? "{\"status\":\"queued\"}" : "{\"error\":\"Enable album and connect Wi-Fi first\"}");
}
static bool still_current(void *context)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = s_config.enabled && s_revision == *(unsigned *)context && display_scheduler_current();
    xSemaphoreGive(s_lock); return valid;
}
static esp_http_client_handle_t open_request(const char *base, const char *path)
{
    char url[448]; snprintf(url, sizeof(url), "%s%s", base, path);
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
display_result_t album_run(void)
{
        xSemaphoreTake(s_lock, portMAX_DELAY);
        config_t config = s_config;
        unsigned revision = s_revision;
        char current[65]; strlcpy(current,s_version,sizeof(current));
        TickType_t period = (TickType_t)((uint64_t)config.interval * configTICK_RATE_HZ);
        bool due = s_force || !current[0] || (xTaskGetTickCount()-s_last_refresh) >= period;
        if (config.enabled && due) s_loading = true;
        xSemaphoreGive(s_lock);
        if (!config.enabled || !due || !wifi_lan_connected() || (strncmp(config.url,"https://",8)==0 && time(NULL)<1700000000)) {
            xSemaphoreTake(s_lock, portMAX_DELAY); s_loading=false; xSemaphoreGive(s_lock); return display_result(!config.enabled?DISPLAY_WAIT_FOREVER:!due?(uint32_t)(period-(xTaskGetTickCount()-s_last_refresh))*portTICK_PERIOD_MS:30000,NULL);
        }
        char path[240], photo[33]="", error[80]="";
        int width = epd_profile()->w, height = epd_profile()->h;
        if (settings_get()->panel == EPD_PANEL_A1 && settings_get()->a1_mode == 1) { width=768; height=552; }
        snprintf(path,sizeof(path),"/v1/album/next.json?playlist=%s&current=%s&mode=%s&width=%d&height=%d",
                 config.playlist,current,config.random ? "random" : "sequence",width,height);
        esp_http_client_handle_t client = open_request(config.url,path);
        bool ok = false;
        if (client) {
            char body[1025]; int used=0,n;
            while (used<1024 && (n=esp_http_client_read(client,body+used,1024-used))>0) used+=n;
            body[used]=0;
            cJSON *json=cJSON_Parse(body), *id=cJSON_GetObjectItemCaseSensitive(json,"id");
            ok=cJSON_IsString(id) && strlen(id->valuestring)==32;
            if (ok) {
                for (const char *p=id->valuestring; *p; ++p)
                    if (!((*p>='0' && *p<='9') || (*p>='a' && *p<='f'))) ok=false;
                if (ok) strlcpy(photo,id->valuestring,sizeof(photo));
            }
            cJSON_Delete(json); esp_http_client_close(client); esp_http_client_cleanup(client);
        }
        if (ok && strcmp(photo,current)) {
            snprintf(path,sizeof(path),"/v1/album/photos/%s.bin",photo);
            client=open_request(config.url,path);
            ok=client && frame_download_display(client,still_current,&revision)==0;
            if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
        }
        if (!ok) strlcpy(error,"Album unavailable, empty or frame refresh failed",sizeof(error));
        xSemaphoreTake(s_lock,portMAX_DELAY);
        s_loading=false;
        if (revision==s_revision && s_config.enabled) {
            strlcpy(s_error,error,sizeof(s_error));
            if (ok) { strlcpy(s_version,photo,sizeof(s_version)); s_last_refresh=xTaskGetTickCount(); s_force=false; }
        }
        xSemaphoreGive(s_lock);
        return display_result(ok?config.interval*1000:30000,ok?NULL:error);
}
void album_init(void)
{
    s_lock=xSemaphoreCreateMutex(); ESP_ERROR_CHECK(s_lock ? ESP_OK : ESP_ERR_NO_MEM);
    nvs_handle_t nvs; size_t size=sizeof(s_config);
    if (nvs_open("album",NVS_READONLY,&nvs)==ESP_OK) {
        if (nvs_get_blob(nvs,"config_v1",&s_config,&size)!=ESP_OK || size!=sizeof(s_config)) s_config = (config_t){.interval=1800};
        nvs_close(nvs);
    }

}
void album_register(httpd_handle_t server)
{
    const httpd_uri_t routes[]={
        {.uri="/api/album",.method=HTTP_GET,.handler=get_handler},
        {.uri="/api/album",.method=HTTP_POST,.handler=config_handler},
        {.uri="/api/album/refresh",.method=HTTP_POST,.handler=refresh_handler},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i) ESP_ERROR_CHECK(auth_register_uri_handler(server,&routes[i]));
}
