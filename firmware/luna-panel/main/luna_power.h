#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Called only by app startup / the serialized LVGL thread. No screen/radio API.
esp_err_t luna_power_init(void);
esp_err_t luna_power_set_idle(bool idle);
