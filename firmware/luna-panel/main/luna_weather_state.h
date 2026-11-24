#pragma once
#include <stdbool.h>

// Keep the original order and sizes: persisted weather v1/v2 NVS ABI.
typedef struct {
    bool configured;
    bool available;
    bool stale;
    bool coordinates_valid;
    double latitude;
    double longitude;
    int temperature_c;
    char location[48];
    char condition[48];
    char observed_at[48];
    char summary[128];
    char details[128];
} luna_weather_state_t;

// Typed UI readings. Old NVS snapshots lack these; show -- until fresh HTTPS.
typedef struct {
    bool available, rain_available;
    int apparent_c, humidity_percent, wind_decims_ms;
    int high_c, low_c, rain_percent, weather_code;
} luna_weather_metrics_t;
