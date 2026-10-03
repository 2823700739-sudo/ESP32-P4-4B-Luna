// DC always-bright standby: retain LCD/PSRAM clocks, reduce only CPU workload.
#include "sdkconfig.h"
#include "esp_pm.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "luna_power.h"

static const char *TAG="luna_power";
static bool ready,idle_profile;

static esp_err_t configure(bool idle)
{
    // Hardware trial of 180MHz produced DSI underrun and an unresponsive UI.
    // Retain the display-safe CPU clock; standby savings come from less work.
    // Do not reconfigure clocks on standby/wake while live DSI is scanning.
    if(ready){
        idle_profile=idle;
        ESP_LOGI(TAG,"%s: CPU=%dMHz; reduced-work standby; brightness/radios unchanged",
                 idle?"STANDBY":"ACTIVE",esp_clk_cpu_freq()/1000000);
        return ESP_OK;
    }
    const int mhz=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    const esp_pm_config_t config={.max_freq_mhz=mhz,.min_freq_mhz=mhz,.light_sleep_enable=false};
    esp_err_t rc=esp_pm_configure(&config);
    if(rc!=ESP_OK){ESP_LOGW(TAG,"Profile change failed rc=%d; keep previous clock profile",rc);return rc;}
    idle_profile=idle;
    ESP_LOGI(TAG,"%s: CPU=%dMHz; display brightness unchanged; light sleep OFF; radios unchanged",
             idle?"STANDBY":"ACTIVE",esp_clk_cpu_freq()/1000000);
    return ESP_OK;
}
esp_err_t luna_power_init(void)
{
    ready=false;
    esp_err_t rc=configure(false);ready=rc==ESP_OK;return rc;
}
esp_err_t luna_power_set_idle(bool idle)
{
    if(!ready)return ESP_ERR_INVALID_STATE;
    return idle==idle_profile?ESP_OK:configure(idle);
}
