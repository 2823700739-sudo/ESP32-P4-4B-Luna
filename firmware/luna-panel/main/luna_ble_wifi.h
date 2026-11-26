#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "luna_weather_config.h"

typedef struct {
    bool online, configured, available, cached;
} luna_ble_wifi_status_t;

typedef struct {
    char setup_id[33];
    bool saved, wifi_configured;
    unsigned state; /* 0 idle, 1 saving, 2 saved, 3 apply_failed, 4 save_failed */
    int error;
} luna_ble_wifi_setup_status_t;

esp_err_t luna_ble_wifi_start(void);
luna_ble_wifi_status_t luna_ble_wifi_status(void);
bool luna_ble_wifi_configure(const luna_weather_config_t *config);
bool luna_ble_wifi_setup_status(luna_ble_wifi_setup_status_t *out);
/* A phone can test a network in RAM before choosing a city; never commits a trial. */
bool luna_ble_wifi_try_network(const luna_weather_config_t *config);
