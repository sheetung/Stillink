#include "display_scheduler.h"
#include "weather.h"
#include "music.h"
#include "album.h"
#include "dates.h"
#include "frame.h"
#include "connection_card.h"
#include "wifi_lan.h"
#include "online_ota.h"
#include "power.h"
#include "auth.h"
#include "service_access.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum { JOB_NONE,JOB_AUTO,JOB_MANUAL,JOB_CLEAR,JOB_CONNECTION,JOB_RESET,JOB_PULL } job_t;
typedef enum { MODE_NONE,MODE_WEATHER,MODE_MUSIC,MODE_ALBUM,MODE_DATES } display_mode_t;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t scheduler_task,executor_task;
static SemaphoreHandle_t reset_done;
static bool running,armed=true,manual_pending,clear_pending,connection_pending,reset_pending,pull_pending;
static TickType_t deadline;
static job_t queued,current_job;
static uint32_t generation,job_generation,sequence,queued_id,active_id,completed_id,manual_id,clear_id,connection_id,reset_id,pull_id;
static int clear_cycles=2;
static char pull_path[512],queued_path[512];
static uint32_t queued_generation,wake_sequence;
static char last_error[80];
typedef struct {uint32_t id;char error[80];} completed_task_t;
static completed_task_t history[8];static unsigned history_index;

static const char *job_name(job_t j){switch(j){case JOB_AUTO:return "automatic";case JOB_MANUAL:return "manual";case JOB_CLEAR:return "clear";case JOB_CONNECTION:return "connection";case JOB_RESET:return "reset_notice";case JOB_PULL:return "service_frame";default:return "idle";}}
static display_mode_t selected_mode(void){if(dates_enabled())return MODE_DATES;if(album_enabled())return MODE_ALBUM;if(music_enabled())return MODE_MUSIC;if(weather_enabled())return MODE_WEATHER;return MODE_NONE;}
static const char *mode_name(display_mode_t m){switch(m){case MODE_DATES:return "daily";case MODE_ALBUM:return "album";case MODE_MUSIC:return "music";case MODE_WEATHER:return "weather";default:return "manual";}}
static void wake_task(void){TaskHandle_t t;portENTER_CRITICAL(&mux);t=scheduler_task;portEXIT_CRITICAL(&mux);if(t)xTaskNotifyGive(t);}
void display_scheduler_wake(void){portENTER_CRITICAL(&mux);++wake_sequence;armed=true;deadline=xTaskGetTickCount();portEXIT_CRITICAL(&mux);wake_task();}
void display_scheduler_changed(void){portENTER_CRITICAL(&mux);++generation;last_error[0]=0;portEXIT_CRITICAL(&mux);display_scheduler_wake();}
bool display_scheduler_current(void){portENTER_CRITICAL(&mux);bool ok=job_generation==generation;portEXIT_CRITICAL(&mux);return ok;}
uint32_t display_scheduler_manual_ready(void){portENTER_CRITICAL(&mux);manual_pending=true;pull_pending=false;clear_pending=false;connection_pending=false;manual_id=++sequence;uint32_t id=manual_id;++generation;portEXIT_CRITICAL(&mux);wake_task();return id;}
uint32_t display_scheduler_clear(int cycles){portENTER_CRITICAL(&mux);if(!clear_pending)clear_id=++sequence;clear_pending=true;clear_cycles=cycles<1?1:cycles>4?4:cycles;uint32_t id=clear_id;++generation;portEXIT_CRITICAL(&mux);wake_task();return id;}
void display_scheduler_connection(void){portENTER_CRITICAL(&mux);connection_pending=true;connection_id=++sequence;++generation;portEXIT_CRITICAL(&mux);wake_task();}
void display_scheduler_reset_notice(void){portENTER_CRITICAL(&mux);reset_pending=true;reset_id=++sequence;++generation;portEXIT_CRITICAL(&mux);wake_task();if(reset_done)xSemaphoreTake(reset_done,portMAX_DELAY);}
static void scheduler(void *arg){
    for(;;){
        TickType_t wait=portMAX_DELAY;bool dispatch=false;
        bool updating=online_ota_busy();
        portENTER_CRITICAL(&mux);
        if(updating){wait=pdMS_TO_TICKS(1000);}
        else if(!running&&queued==JOB_NONE){
            if(manual_pending){queued=JOB_MANUAL;queued_id=manual_id;manual_pending=false;}
            else if(pull_pending){queued=JOB_PULL;queued_id=pull_id;pull_pending=false;}
            else if(reset_pending){queued=JOB_RESET;queued_id=reset_id;reset_pending=false;}
            else if(connection_pending){queued=JOB_CONNECTION;queued_id=connection_id;connection_pending=false;}
            else if(clear_pending){queued=JOB_CLEAR;queued_id=clear_id;clear_pending=false;}
            else if(armed){int32_t remaining=(int32_t)(deadline-xTaskGetTickCount());if(remaining<=0){queued=JOB_AUTO;queued_id=++sequence;armed=false;}else wait=(TickType_t)remaining;}
            if(queued!=JOB_NONE){queued_generation=generation;if(queued==JOB_PULL)strlcpy(queued_path,pull_path,sizeof(queued_path));dispatch=true;}
        }
        portEXIT_CRITICAL(&mux);
        if(dispatch)xTaskNotifyGive(executor_task);
        ulTaskNotifyTake(pdTRUE,wait);
    }
}
static bool pull_current(void *unused){return display_scheduler_current();}
static display_result_t pull_frame(const char *path){
    char base[192],url[720];service_access_base(base,sizeof(base));
    if(!base[0])return display_result(DISPLAY_WAIT_FOREVER,"Configure extra service first");
    snprintf(url,sizeof(url),"%s%s",base,path);
    esp_http_client_config_t options={.url=url,.timeout_ms=12000,.buffer_size=1024,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t c=esp_http_client_init(&options);bool ok=false;
    if(c){service_access_attach(c,url);if(esp_http_client_open(c,0)==ESP_OK&&esp_http_client_fetch_headers(c)>=0&&esp_http_client_get_status_code(c)==200)ok=frame_download_display(c,pull_current,NULL)==0;esp_http_client_close(c);esp_http_client_cleanup(c);}
    if(ok&&display_scheduler_current()){weather_disable();music_disable();album_disable();dates_disable();}
    return display_result(DISPLAY_WAIT_FOREVER,ok?NULL:"Service frame download or display failed");
}
static void executor(void *arg){
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        portENTER_CRITICAL(&mux);job_t job=queued;queued=JOB_NONE;current_job=job;active_id=queued_id;job_generation=queued_generation;running=job!=JOB_NONE;uint32_t observed_wake=wake_sequence;int cycles=clear_cycles;char path[512];strlcpy(path,queued_path,sizeof(path));portEXIT_CRITICAL(&mux);
        if(job==JOB_NONE)continue;
        display_result_t result=display_result(DISPLAY_WAIT_FOREVER,NULL);
        power_busy(true);
        bool deferred=online_ota_busy();
        if(deferred){
            result.next_ms=1000;
            portENTER_CRITICAL(&mux);
            if(job==JOB_MANUAL){manual_pending=true;manual_id=active_id;}
            if(job==JOB_CLEAR){clear_pending=true;clear_id=active_id;}
            if(job==JOB_CONNECTION){connection_pending=true;connection_id=active_id;}
            if(job==JOB_RESET){reset_pending=true;reset_id=active_id;}
            if(job==JOB_PULL&&!pull_pending){pull_pending=true;pull_id=active_id;strlcpy(pull_path,path,sizeof(pull_path));}
            portEXIT_CRITICAL(&mux);
        }else if(job==JOB_MANUAL){if(frame_display_now()!=0)result=display_result(DISPLAY_WAIT_FOREVER,"Manual screen refresh failed");}
        else if(job==JOB_CLEAR){if(frame_clear_cycles(cycles)!=0)result=display_result(DISPLAY_WAIT_FOREVER,"Screen clear failed");}
        else if(job==JOB_PULL)result=pull_frame(path);
        else if(job==JOB_CONNECTION||job==JOB_RESET){if(connection_card_render(job==JOB_RESET)!=0)result=display_result(DISPLAY_WAIT_FOREVER,"Connection card refresh failed");if(job==JOB_RESET)xSemaphoreGive(reset_done);}
        else if(connection_card_holding()||!wifi_lan_connected())result.next_ms=DISPLAY_WAIT_FOREVER;
        else switch(selected_mode()){
            case MODE_WEATHER:result=weather_run();break;
            case MODE_MUSIC:result=music_run();break;
            case MODE_ALBUM:result=album_run();break;
            case MODE_DATES:result=dates_run();break;
            default:break;
        }
        power_busy(false);
        portENTER_CRITICAL(&mux);
        running=false;current_job=JOB_NONE;
        if(!deferred){completed_id=active_id;history[history_index].id=active_id;strlcpy(history[history_index].error,result.error,sizeof(history[history_index].error));history_index=(history_index+1)%8;}

        if(job_generation==generation){strlcpy(last_error,result.error,sizeof(last_error));if(job==JOB_AUTO&&observed_wake==wake_sequence){armed=result.next_ms!=DISPLAY_WAIT_FOREVER;if(armed)deadline=xTaskGetTickCount()+pdMS_TO_TICKS(result.next_ms?result.next_ms:1);}else if(job!=JOB_AUTO){armed=true;deadline=xTaskGetTickCount();}}
        portEXIT_CRITICAL(&mux);wake_task();
    }
}
static esp_err_t status_handler(httpd_req_t *r){
    power_activity();httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");
    cJSON *j=cJSON_CreateObject();if(!j)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(j,"mode",mode_name(selected_mode()));
    portENTER_CRITICAL(&mux);bool busy=running,pending=queued!=JOB_NONE||manual_pending||clear_pending||connection_pending||reset_pending||pull_pending,scheduled=armed;job_t op=current_job;uint32_t id=active_id,done=completed_id;TickType_t due=deadline;char error[80];strlcpy(error,last_error,sizeof(error));portEXIT_CRITICAL(&mux);
    cJSON_AddStringToObject(j,"status",busy?"running":pending?"queued":error[0]?"error":"idle");cJSON_AddStringToObject(j,"operation",job_name(op));cJSON_AddBoolToObject(j,"busy",busy||pending);cJSON_AddNumberToObject(j,"task_id",id);cJSON_AddNumberToObject(j,"completed_id",done);cJSON_AddStringToObject(j,"error",error);
    if(scheduled){int32_t remaining=(int32_t)(due-xTaskGetTickCount());cJSON_AddNumberToObject(j,"next_update_in_s",remaining>0?remaining/configTICK_RATE_HZ:0);}else cJSON_AddNullToObject(j,"next_update_in_s");
    char query[64],value[16];uint32_t wanted=0;
    if(httpd_req_get_url_query_str(r,query,sizeof(query))==ESP_OK&&httpd_query_key_value(query,"task_id",value,sizeof(value))==ESP_OK)wanted=strtoul(value,NULL,10);
    if(wanted){
        bool found=false,waiting=false,executing=false;char task_error[80]="";
        portENTER_CRITICAL(&mux);
        for(unsigned i=0;i<8;i++)if(history[i].id==wanted){found=true;strlcpy(task_error,history[i].error,sizeof(task_error));}
        executing=running&&active_id==wanted;
        waiting=(queued!=JOB_NONE&&queued_id==wanted)||(manual_pending&&manual_id==wanted)||(pull_pending&&pull_id==wanted)||(clear_pending&&clear_id==wanted)||(connection_pending&&connection_id==wanted)||(reset_pending&&reset_id==wanted);
        portEXIT_CRITICAL(&mux);
        cJSON_AddStringToObject(j,"task_status",found?(task_error[0]?"failed":"completed"):executing?"running":waiting?"queued":"superseded");
        cJSON_AddStringToObject(j,"task_error",task_error);
    }
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return ESP_ERR_NO_MEM;esp_err_t e=httpd_resp_sendstr(r,body);free(body);return e;
}
void display_scheduler_init(void){
    reset_done=xSemaphoreCreateBinary();configASSERT(reset_done);
    ESP_ERROR_CHECK(xTaskCreate(executor,"display_exec",9216,NULL,4,&executor_task)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(scheduler,"display_sched",2048,NULL,3,&scheduler_task)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    display_scheduler_wake();
}
static bool valid_frame_path(const char *path){
    if(strncmp(path,"/v1/",4)||strstr(path,"..")||strchr(path,'#')||strchr(path,'\\'))return false;
    const char *q=strchr(path,'?');size_t n=q?(size_t)(q-path):strlen(path);if(n<4||strncmp(path+n-4,".bin",4))return false;
    for(const unsigned char *p=(const unsigned char*)path;*p;p++)if(*p<=32||*p>=127)return false;
    return true;
}
static esp_err_t pull_handler(httpd_req_t *r){
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Access-Control-Allow-Origin","*");
    if(r->content_len<2||r->content_len>768)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid body");
    char body[769];int used=0;while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}body[used]=0;
    cJSON *j=cJSON_Parse(body),*p=cJSON_GetObjectItemCaseSensitive(j,"path");bool valid=cJSON_IsString(p)&&strlen(p->valuestring)<sizeof(pull_path)&&valid_frame_path(p->valuestring);
    if(!valid){cJSON_Delete(j);return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid service frame path");}
    if(online_ota_busy()){cJSON_Delete(j);httpd_resp_set_status(r,"409 Conflict");return httpd_resp_sendstr(r,"{\"error\":\"Firmware upgrade in progress\"}");}
    connection_card_release();
    portENTER_CRITICAL(&mux);strlcpy(pull_path,p->valuestring,sizeof(pull_path));pull_pending=true;pull_id=++sequence;uint32_t id=pull_id;++generation;portEXIT_CRITICAL(&mux);cJSON_Delete(j);wake_task();
    snprintf(body,sizeof(body),"{\"requested\":true,\"task_id\":%lu}",(unsigned long)id);httpd_resp_set_status(r,"202 Accepted");return httpd_resp_sendstr(r,body);
}
void display_scheduler_register(httpd_handle_t server){
    const httpd_uri_t routes[]={{.uri="/api/display",.method=HTTP_GET,.handler=status_handler},{.uri="/api/display/pull",.method=HTTP_POST,.handler=pull_handler}};
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++)ESP_ERROR_CHECK(auth_register_uri_handler(server,&routes[i]));
}
