// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <stddef.h>
#include <stdint.h>

#define LUNA_BLE_MAX_MESSAGE 4096
#define LUNA_BLE_HEADER_SIZE 12
#define LUNA_BLE_FRAME_TIMEOUT_MS 30000

typedef struct {
    uint16_t id, total, received;
    uint32_t crc, started_ms;
    uint8_t data[LUNA_BLE_MAX_MESSAGE + 1];
    uint8_t seen[LUNA_BLE_MAX_MESSAGE / 8];
} luna_ble_rx_t;

// Returns -1 (reject), 0 (incomplete), or 1 (complete, data retained until next feed).
int luna_ble_frame_feed(luna_ble_rx_t *rx, const uint8_t *packet, size_t len, uint32_t now_ms);
void luna_ble_frame_reset(luna_ble_rx_t *rx);
uint32_t luna_ble_crc32(const uint8_t *data, size_t len);
size_t luna_ble_frame_encode(uint8_t *out, size_t capacity, const uint8_t *data,
                             size_t total, uint16_t id, size_t offset, size_t att_payload);
