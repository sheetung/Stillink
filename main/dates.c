#include "display_scheduler.h"
#include "dates.h"
#include "auth.h"
#include "album.h"
#include "music.h"
#include "weather.h"
#include "service_access.h"
#include "wifi_lan.h"
#include "connection_card.h"
#include "frame.h"
#include "power.h"
#include "online_ota.h"
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
#include <stdio.h>
#include <time.h>

typedef struct { bool enabled; int utc_offset; char path[512]; } config_t;
static config_t config={.utc_offset=28800};
static SemaphoreHandle_t lock;
static unsigned revision;
static bool loading,force;
static int64_t last_day=-1;
static TickType_t retry;
static char error[80];
static esp_err_t save(const config_t *next){
    nvs_handle_t n;esp_err_t e=nvs_open("dates",NVS_READWRITE,&n);
    if(e==ESP_OK){e=nvs_set_blob(n,"config_v1",next,sizeof(*next));if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);}return e;
}
bool dates_enabled(void){if(!lock)return false;xSemaphoreTake(lock,portMAX_DELAY);bool value=config.enabled;xSemaphoreGive(lock);return value;}
esp_err_t dates_disable(void){
    if(!lock)return ESP_OK;
    xSemaphoreTake(lock,portMAX_DELAY);esp_err_t e=ESP_OK;
    if(config.enabled){config_t next=config;next.enabled=false;e=save(&next);if(e==ESP_OK){config=next;++revision;force=false;error[0]=0;}}
    xSemaphoreGive(lock);if(e==ESP_OK)display_scheduler_changed();return e;
}
void dates_factory_reset(void){nvs_handle_t n;if(nvs_open("dates",NVS_READWRITE,&n)==ESP_OK){nvs_erase_all(n);nvs_commit(n);nvs_close(n);}}
static void headers(httpd_req_t *r){power_activity();httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");}
static esp_err_t get_config(httpd_req_t *r){
    headers(r);cJSON *j=cJSON_CreateObject();if(!j)return ESP_ERR_NO_MEM;
    xSemaphoreTake(lock,portMAX_DELAY);
    cJSON_AddBoolToObject(j,"enabled",config.enabled);cJSON_AddStringToObject(j,"path",config.path);cJSON_AddNumberToObject(j,"utc_offset",config.utc_offset);
    cJSON_AddStringToObject(j,"mode",strstr(config.path,"/anniversary.bin?")?"anniversary":"calendar");
    cJSON_AddStringToObject(j,"status",!config.enabled?"disabled":!wifi_lan_connected()?"waiting_wifi":time(NULL)<1700000000?"waiting_time":loading?"loading":error[0]?"error":last_day<0?"waiting":"ready");
    cJSON_AddStringToObject(j,"error",error);xSemaphoreGive(lock);
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return ESP_ERR_NO_MEM;
    esp_err_t e=httpd_resp_sendstr(r,body);free(body);return e;
}
static bool valid_path(const char *p){
    if(strncmp(p,"/v1/calendar/calendar.bin?",strlen("/v1/calendar/calendar.bin?")) && strncmp(p,"/v1/calendar/anniversary.bin?",strlen("/v1/calendar/anniversary.bin?")))return false;
    if(strstr(p,"..")||strchr(p,'#')||strchr(p,'\\'))return false;
    for(;*p;p++)if((unsigned char)*p<=32 || (unsigned char)*p>=127)return false;
    return true;
}
static esp_err_t set_config(httpd_req_t *r){
    headers(r);if(r->content_len<2||r->content_len>1024)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid body");
    char body[1025];int used=0;
    while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}body[used]=0;
    cJSON *j=cJSON_Parse(body),*on=cJSON_GetObjectItemCaseSensitive(j,"enabled"),*path=cJSON_GetObjectItemCaseSensitive(j,"path"),*offset=cJSON_GetObjectItemCaseSensitive(j,"utc_offset");
    xSemaphoreTake(lock,portMAX_DELAY);config_t next=config;xSemaphoreGive(lock);
    bool valid=cJSON_IsBool(on);if(valid)next.enabled=cJSON_IsTrue(on);
    if(path){valid=valid&&cJSON_IsString(path)&&strlen(path->valuestring)<sizeof(next.path)&&valid_path(path->valuestring);if(valid)strlcpy(next.path,path->valuestring,sizeof(next.path));}
    if(offset){valid=valid&&cJSON_IsNumber(offset)&&offset->valuedouble==offset->valueint&&offset->valueint>=-43200&&offset->valueint<=50400;if(valid)next.utc_offset=offset->valueint;}
    if(next.enabled&&!valid_path(next.path))valid=false;
    cJSON_Delete(j);if(!valid)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid daily card settings");
    char base[192];service_access_base(base,sizeof(base));if(next.enabled&&!base[0])return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Configure service first");
    esp_err_t e=ESP_OK;
    if(next.enabled){e=weather_disable();if(e==ESP_OK)e=music_disable();if(e==ESP_OK)e=album_disable();}
    if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not disable other cards");
    xSemaphoreTake(lock,portMAX_DELAY);e=save(&next);if(e==ESP_OK){config=next;++revision;force=next.enabled;retry=0;error[0]=0;last_day=-1;}xSemaphoreGive(lock);
    if(e!=ESP_OK)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save daily card");
    connection_card_release();display_scheduler_changed();return httpd_resp_sendstr(r,"{\"saved\":true}");
}
static esp_err_t refresh(httpd_req_t *r){headers(r);xSemaphoreTake(lock,portMAX_DELAY);bool ok=config.enabled&&wifi_lan_connected();if(ok){force=true;retry=0;}xSemaphoreGive(lock);if(ok)display_scheduler_changed();httpd_resp_set_status(r,ok?"202 Accepted":"409 Conflict");return httpd_resp_sendstr(r,ok?"{\"requested\":true}":"{\"error\":\"Enable daily card and connect Wi-Fi first\"}");}
static bool current(void *ctx){xSemaphoreTake(lock,portMAX_DELAY);bool ok=config.enabled&&revision==*(unsigned*)ctx&&display_scheduler_current();xSemaphoreGive(lock);return ok;}
display_result_t dates_run(void){
        if(time(NULL)<1700000000)return display_result(30000,NULL);
        xSemaphoreTake(lock,portMAX_DELAY);config_t snapshot=config;unsigned rev=revision;time_t now=time(NULL);int64_t day=((int64_t)now+snapshot.utc_offset)/86400;
        bool due=snapshot.enabled&&(force||day!=last_day)&&(!retry||(int32_t)(xTaskGetTickCount()-retry)>=0);if(due)loading=true;xSemaphoreGive(lock);
        uint32_t midnight=(uint32_t)((86400-(((int64_t)now+snapshot.utc_offset)%86400))*1000);
        if(!due){if(!snapshot.enabled)return display_result(DISPLAY_WAIT_FOREVER,NULL);int32_t remaining=(int32_t)(retry-xTaskGetTickCount());return display_result(retry&&remaining>0?(uint32_t)remaining*portTICK_PERIOD_MS:midnight,NULL);}
        char base[192],url[900],date[16],month[12];service_access_base(base,sizeof(base));
        time_t local=now+snapshot.utc_offset;struct tm tm;gmtime_r(&local,&tm);strftime(date,sizeof(date),"%Y-%m-%d",&tm);strftime(month,sizeof(month),"%Y-%m",&tm);
        int w=epd_profile()->w,h=epd_profile()->h;if(settings_get()->panel==EPD_PANEL_A1&&settings_get()->a1_mode==1){w=768;h=552;}
        snprintf(url,sizeof(url),"%s%s&date=%s&month=%s&width=%d&height=%d&panel=%d",base,snapshot.path,date,month,w,h,settings_get()->panel);
        esp_http_client_config_t opts={.url=url,.timeout_ms=12000,.buffer_size=1024,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
        esp_http_client_handle_t client=base[0]?esp_http_client_init(&opts):NULL;bool ok=false;
        if(client){service_access_attach(client,url);if(esp_http_client_open(client,0)==ESP_OK&&esp_http_client_fetch_headers(client)>=0&&esp_http_client_get_status_code(client)==200)ok=frame_download_display(client,current,&rev)==0;esp_http_client_close(client);esp_http_client_cleanup(client);}
        xSemaphoreTake(lock,portMAX_DELAY);loading=false;
        if(rev==revision&&config.enabled){if(ok){last_day=day;force=false;retry=0;error[0]=0;}else{strlcpy(error,"Daily card download or refresh failed",sizeof(error));retry=xTaskGetTickCount()+pdMS_TO_TICKS(60000);}}
        xSemaphoreGive(lock);
        time_t finished=time(NULL);midnight=(uint32_t)((86400-(((int64_t)finished+snapshot.utc_offset)%86400))*1000);
        return display_result(ok?midnight:60000,ok?NULL:"Daily card download or refresh failed");
}
void dates_init(void){lock=xSemaphoreCreateMutex();ESP_ERROR_CHECK(lock?ESP_OK:ESP_ERR_NO_MEM);nvs_handle_t n;size_t size=sizeof(config);if(nvs_open("dates",NVS_READONLY,&n)==ESP_OK){config_t stored={0};if(nvs_get_blob(n,"config_v1",&stored,&size)==ESP_OK&&size==sizeof(stored)&&stored.utc_offset>=-43200&&stored.utc_offset<=50400&&(!stored.enabled||valid_path(stored.path)))config=stored;nvs_close(n);}}
void dates_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/dates",.method=HTTP_GET,.handler=get_config},{.uri="/api/dates",.method=HTTP_POST,.handler=set_config},{.uri="/api/dates/refresh",.method=HTTP_POST,.handler=refresh}};for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++)ESP_ERROR_CHECK(auth_register_uri_handler(server,&routes[i]));}
