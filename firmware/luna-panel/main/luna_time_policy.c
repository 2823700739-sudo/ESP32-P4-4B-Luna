#include "luna_time_policy.h"
bool luna_time_epoch_valid(int64_t ms) { return ms >= 1700000000000LL && ms < 4102444800000LL; }
bool luna_time_may_sync(const luna_time_policy_t *p, luna_time_source_t source, int64_t now)
{
    if (!p || now < 0 || (source != LUNA_TIME_BLE && source != LUNA_TIME_NTP)) return false;
    return source == LUNA_TIME_BLE || !p->ble_seen || now - p->last_ble_us >= 120000000LL;
}
void luna_time_did_sync(luna_time_policy_t *p, luna_time_source_t source, int64_t now)
{
    p->valid=true; p->source=source; p->last_sync_us=now;
    if (source == LUNA_TIME_BLE) { p->ble_seen=true; p->last_ble_us=now; }
}
