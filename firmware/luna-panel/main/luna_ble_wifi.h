#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool online, configured, available, cached;
} luna_ble_wifi_status_t;

esp_err_t luna_ble_wifi_start(void);
luna_ble_wifi_status_t luna_ble_wifi_status(void);
