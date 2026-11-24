#include "esp_pm.h"
static esp_pm_config_t current;
static unsigned calls;
static int result;
esp_err_t esp_pm_configure(const esp_pm_config_t *config){calls++;if(result)return result;current=*config;return ESP_OK;}
int esp_clk_cpu_freq(void){return current.max_freq_mhz*1000000;}
void luna_test_fail(int value){result=value;}
unsigned luna_test_calls(void){return calls;}
int luna_test_max(void){return current.max_freq_mhz;}
int luna_test_min(void){return current.min_freq_mhz;}
int luna_test_sleep(void){return current.light_sleep_enable;}
