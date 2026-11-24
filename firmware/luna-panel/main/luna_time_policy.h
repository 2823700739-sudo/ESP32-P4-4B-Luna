#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { LUNA_TIME_WAITING, LUNA_TIME_BLE, LUNA_TIME_NTP } luna_time_source_t;
typedef struct {
    bool valid, ble_seen;
    luna_time_source_t source;
    int64_t last_sync_us, last_ble_us;
} luna_time_policy_t;
bool luna_time_epoch_valid(int64_t epoch_ms);
bool luna_time_may_sync(const luna_time_policy_t *policy, luna_time_source_t source, int64_t now_us);
void luna_time_did_sync(luna_time_policy_t *policy, luna_time_source_t source, int64_t now_us);
