// Sole B1 system clock writer; SNTP weak hook arbitrates BEFORE setting time.
#include <stdatomic.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "luna_time.h"

static const char *TAG="luna_time";
static SemaphoreHandle_t mutex;
static luna_time_policy_t policy;
static atomic_bool network;

esp_err_t luna_time_init(void)
{
    if (mutex) return ESP_ERR_INVALID_STATE;
    mutex=xSemaphoreCreateMutex(); return mutex ? ESP_OK : ESP_ERR_NO_MEM;
}
const char *luna_time_source_name(luna_time_source_t source)
{
    return source == LUNA_TIME_BLE ? "ble" : source == LUNA_TIME_NTP ? "ntp" : "waiting";
}
luna_time_policy_t luna_time_snapshot(void)
{
    luna_time_policy_t result={0};
    if (mutex && xSemaphoreTake(mutex,pdMS_TO_TICKS(5)) == pdTRUE) {
        result=policy; xSemaphoreGive(mutex);
    }
    return result;
}
static bool apply(int64_t ms, luna_time_source_t source)
{
    if (!mutex || !luna_time_epoch_valid(ms) || xSemaphoreTake(mutex,pdMS_TO_TICKS(5)) != pdTRUE) return false;
    int64_t now=esp_timer_get_time(); bool ok=false;
    luna_time_source_t previous=policy.source;
    if (luna_time_may_sync(&policy,source,now)) {
        struct timeval tv={.tv_sec=ms/1000,.tv_usec=(ms%1000)*1000};
        ok=settimeofday(&tv,NULL)==0;
        if (ok) luna_time_did_sync(&policy,source,now);
    }
    xSemaphoreGive(mutex);
    if (ok && previous != source) ESP_LOGI(TAG,"Clock source switched to %s",luna_time_source_name(source));
    return ok;
}
bool luna_time_accept_ble(int64_t ms) { return apply(ms,LUNA_TIME_BLE); }

// IDF 6.0.2 documented weak replacement. No default SNTP write precedes arbitration.
void sntp_sync_time(struct timeval *tv)
{
    if (tv && tv->tv_usec >= 0 && tv->tv_usec < 1000000 && atomic_load(&network) &&
        tv->tv_sec >= 1700000000 && tv->tv_sec < 4102444800LL &&
        apply((int64_t)tv->tv_sec * 1000 + tv->tv_usec/1000,LUNA_TIME_NTP))
        esp_sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}
void luna_time_set_network_ready(bool online)
{
    atomic_store(&network,online);
    // Called only on the serialized default event-loop task, never with mutex held.
    if (online && !esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0,"pool.ntp.org");
        esp_sntp_set_sync_interval(3600000);
        esp_sntp_init(); ESP_LOGI(TAG,"Wi-Fi NTP backup started (BLE freshness has priority)");
    } else if (!online && esp_sntp_enabled()) esp_sntp_stop();
}
