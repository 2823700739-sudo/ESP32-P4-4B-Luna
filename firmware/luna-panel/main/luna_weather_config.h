#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

/* One versioned NVS blob is authoritative for credentials AND location. */
typedef struct {
    uint32_t version;
    char setup_id[33];
    char ssid[33];
    char password[65];
    char location[48];
    double latitude, longitude;
} luna_weather_config_t;

bool luna_weather_config_valid(const luna_weather_config_t *config);
bool luna_weather_config_parse(const cJSON *root, luna_weather_config_t *out);
void luna_weather_config_clear(void *data, unsigned length);
