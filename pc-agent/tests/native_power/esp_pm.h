#pragma once
#include <stdbool.h>
#include "esp_err.h"
#define ESP_ERR_INVALID_STATE 259
typedef struct {int max_freq_mhz,min_freq_mhz;bool light_sleep_enable;} esp_pm_config_t;
esp_err_t esp_pm_configure(const esp_pm_config_t *config);
