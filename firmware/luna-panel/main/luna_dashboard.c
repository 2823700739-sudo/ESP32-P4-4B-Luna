// Strict atomic read-only dashboard state, independent of music action FIFO.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "esp_timer.h"
#include "luna_dashboard.h"
static SemaphoreHandle_t mutex;
static luna_dashboard_state_t state;
static bool number(const cJSON *o,const char *key,double max,double *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(cJSON_IsNull(v)){*out=-1;return true;}
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>max)return false;
    *out=v->valuedouble;return true;
}
static bool string(const cJSON *o,const char *key,char *out,size_t cap)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsString(v)||strlen(v->valuestring)>=cap)return false;
    strlcpy(out,v->valuestring,cap);return true;
}
esp_err_t luna_dashboard_init(void)
{
    mutex=xSemaphoreCreateMutex();
    state.cpu=state.gpu=state.cpu_temp=state.gpu_temp=-1;
    state.ram_used_gb=state.ram_total_gb=state.vram_used_gb=state.vram_total_gb=-1;
    state.primary_remaining=state.weekly_remaining=-1;
    return mutex?ESP_OK:ESP_ERR_NO_MEM;
}
void luna_dashboard_disconnect(void)
{
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);state.online=false;xSemaphoreGive(mutex);
}
bool luna_dashboard_snapshot(luna_dashboard_state_t *out)
{
    if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(5))!=pdTRUE)return false;
    *out=state;xSemaphoreGive(mutex);
    int64_t age=out->received_us?esp_timer_get_time()-out->received_us:INT64_MAX;
    out->online=out->online&&age>=0&&age<6000000;
    out->computer_fresh=out->online&&age/1000+out->computer_age_ms<10000;
    struct timeval tv;gettimeofday(&tv,NULL);
    int64_t quota_age=(int64_t)tv.tv_sec*1000+tv.tv_usec/1000-out->quota_sampled_ms;
    out->quota_fresh=out->online&&out->quota_sampled_ms>0&&quota_age>=-5000&&quota_age<120000;
    return true;
}
int luna_dashboard_message(const cJSON *root,const char *session,const char *boot,char *reply,size_t cap)
{
    const cJSON *pc=cJSON_GetObjectItemCaseSensitive(root,"computer"),*quota=cJSON_GetObjectItemCaseSensitive(root,"quota"),
        *project=cJSON_GetObjectItemCaseSensitive(root,"project");
    luna_dashboard_state_t next={0};double primary,weekly,age,sampled;
    if(!cJSON_IsObject(pc)||!cJSON_IsObject(quota)||!cJSON_IsObject(project)||
       !number(pc,"cpu",100,&next.cpu)||!number(pc,"gpu",100,&next.gpu)||
       !number(pc,"cpu_temp",150,&next.cpu_temp)||!number(pc,"gpu_temp",150,&next.gpu_temp)||
       !number(pc,"ram_used_gb",65536,&next.ram_used_gb)||!number(pc,"ram_total_gb",65536,&next.ram_total_gb)||
       !number(pc,"vram_used_gb",65536,&next.vram_used_gb)||!number(pc,"vram_total_gb",65536,&next.vram_total_gb)||
       !number(pc,"sample_age_ms",60000,&age)||age<0||age!=floor(age)||
       !number(quota,"primary_remaining",100,&primary)||primary!=floor(primary)||
       !number(quota,"weekly_remaining",100,&weekly)||weekly!=floor(weekly)||
       !number(quota,"sampled_at_ms",4102444800000.0,&sampled)||sampled<0||sampled!=floor(sampled)||
       !string(pc,"name",next.gpu_name,sizeof(next.gpu_name))||!string(project,"name",next.project_name,sizeof(next.project_name))||
       (next.ram_used_gb>=0&&(next.ram_total_gb<=0||next.ram_used_gb>next.ram_total_gb))||
       (next.vram_used_gb>=0&&(next.vram_total_gb<=0||next.vram_used_gb>next.vram_total_gb)))return BLE_ATT_ERR_UNLIKELY;
    next.online=true;next.received_us=esp_timer_get_time();next.computer_age_ms=(unsigned)age;
    next.primary_remaining=(int)primary;next.weekly_remaining=(int)weekly;next.quota_sampled_ms=(int64_t)sampled;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(5))!=pdTRUE)return BLE_ATT_ERR_UNLIKELY;
    state=next;xSemaphoreGive(mutex);
    snprintf(reply,cap,"{\"v\":1,\"type\":\"dashboard_snapshot_result\",\"session\":\"%s\",\"boot_id\":\"%s\","
        "\"accepted\":true,\"cpu\":%.1f,\"gpu\":%.1f,\"primary_remaining\":%d,\"weekly_remaining\":%d}",
        session,boot,next.cpu,next.gpu,next.primary_remaining,next.weekly_remaining);
    return 0;
}
