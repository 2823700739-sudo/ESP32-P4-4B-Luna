#pragma once
#include "esp_err.h"
#include "luna_weather_state.h"

// On failure neither output changes. Requires explicit Celsius / m/s units.
esp_err_t luna_weather_parse_forecast(const char *json, luna_weather_state_t *weather,
                                     luna_weather_metrics_t *metrics);
