// SPDX-License-Identifier: CC0-1.0
#include "luna_ble_frame.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }
static void wr16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void wr32(uint8_t *p, uint32_t n) { wr16(p, n); wr16(p + 2, n >> 16); }

uint32_t luna_ble_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

void luna_ble_frame_reset(luna_ble_rx_t *rx) { memset(rx, 0, sizeof(*rx)); }

int luna_ble_frame_feed(luna_ble_rx_t *rx, const uint8_t *p, size_t len, uint32_t now)
{
    if (rx->id && (uint32_t)(now - rx->started_ms) >= LUNA_BLE_FRAME_TIMEOUT_MS) luna_ble_frame_reset(rx);
    if (len <= LUNA_BLE_HEADER_SIZE || len > 512 || p[0] != 0x4C || p[1] != 1) return -1;
    uint16_t id = rd16(p + 2), offset = rd16(p + 4), total = rd16(p + 6);
    uint32_t crc = rd32(p + 8);
    size_t chunk = len - LUNA_BLE_HEADER_SIZE;
    if (!id || !total || total > LUNA_BLE_MAX_MESSAGE || offset + chunk > total) return -1;
    if (rx->id && (rx->id != id || rx->total != total || rx->crc != crc)) return -1;
    if (!rx->id) {
        luna_ble_frame_reset(rx);
        rx->id = id; rx->total = total; rx->crc = crc; rx->started_ms = now;
    }
    for (size_t i = 0; i < chunk; i++) {
        size_t n = offset + i;
        if ((rx->seen[n / 8] & (1u << (n % 8))) && rx->data[n] != p[12 + i]) {
            luna_ble_frame_reset(rx);
            return -1;
        }
    }
    for (size_t i = 0; i < chunk; i++) {
        size_t n = offset + i;
        if (!(rx->seen[n / 8] & (1u << (n % 8)))) {
            rx->seen[n / 8] |= 1u << (n % 8);
            rx->data[n] = p[12 + i]; rx->received++;
        }
    }
    if (rx->received != total) return 0;
    if (luna_ble_crc32(rx->data, total) != crc) { luna_ble_frame_reset(rx); return -1; }
    rx->data[total] = 0;
    rx->id = 0; // Retain completed data for caller; next message resets storage.
    return 1;
}

size_t luna_ble_frame_encode(uint8_t *out, size_t cap, const uint8_t *data,
                             size_t total, uint16_t id, size_t offset, size_t limit)
{
    if (!id || !total || total > LUNA_BLE_MAX_MESSAGE || offset >= total || limit <= 12 || limit > 512) return 0;
    size_t size = total - offset;
    if (size > limit - 12) size = limit - 12;
    if (cap < size + 12) return 0;
    out[0] = 0x4C; out[1] = 1;
    wr16(out + 2, id); wr16(out + 4, offset); wr16(out + 6, total);
    wr32(out + 8, luna_ble_crc32(data, total));
    memcpy(out + 12, data + offset, size);
    return size + 12;
}
