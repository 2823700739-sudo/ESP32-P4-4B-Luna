#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Short-lived open setup hotspot, explicitly selected by the user. Worker owns radio calls. */
typedef struct {
    bool active;
    char ssid[33];
    unsigned remaining_seconds;
    int error;
} luna_setup_ap_state_t;

bool luna_setup_ap_snapshot(luna_setup_ap_state_t *out);
void luna_setup_ap_request(bool open);

#define LUNA_SETUP_NETWORK_LIMIT 16
typedef struct {
    char ssid[33];
    int rssi;
    bool secured;
} luna_setup_network_t;
typedef struct {
    unsigned state; /* 0 idle, 1 queued/scanning, 2 complete, 3 failed */
    int error;
    unsigned count;
    luna_setup_network_t networks[LUNA_SETUP_NETWORK_LIMIT];
} luna_setup_scan_t;
bool luna_setup_scan_request(void);
bool luna_setup_scan_snapshot(luna_setup_scan_t *out);

/* Worker-only lifecycle; never called from an LVGL or BLE callback. */
esp_err_t luna_setup_portal_start(void);
void luna_setup_portal_stop(void);
