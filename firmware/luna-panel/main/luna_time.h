#pragma once
#include "esp_err.h"
#include "luna_time_policy.h"
esp_err_t luna_time_init(void);
bool luna_time_accept_ble(int64_t epoch_ms);
void luna_time_set_network_ready(bool online);
luna_time_policy_t luna_time_snapshot(void);
const char *luna_time_source_name(luna_time_source_t source);
