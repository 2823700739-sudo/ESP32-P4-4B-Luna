#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "luna_weather_state.h"

typedef void (*luna_weather_update_cb_t)(const luna_weather_state_t *state, void *context);

/** Load the device-owned location/cache from NVS and start the Wi-Fi weather worker. */
esp_err_t luna_weather_start(luna_weather_update_cb_t callback, void *context);

/** The PC may provision a location, but never supplies the displayed weather data. */
void luna_weather_set_location(const luna_weather_state_t *pc_weather);

/** Called by Wi-Fi events; a disconnect marks the last result as cached. */
void luna_weather_set_network_ready(bool ready);

/** Copy one coherent device-owned snapshot. Does not initiate a network request. */
bool luna_weather_snapshot(luna_weather_state_t *weather, luna_weather_metrics_t *metrics);
