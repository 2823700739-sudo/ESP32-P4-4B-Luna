// SPDX-License-Identifier: CC0-1.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUNA_USB_PROTOCOL_VERSION 1U
#define LUNA_USB_MAX_PAYLOAD 4096U

typedef enum {
    LUNA_LINK_MESSAGE_HELLO = 1,
    LUNA_LINK_MESSAGE_HELLO_ACK = 2,
    LUNA_LINK_MESSAGE_PING = 3,
    LUNA_LINK_MESSAGE_PONG = 4,
    LUNA_LINK_MESSAGE_TOUCH_TEST = 5,
    LUNA_LINK_MESSAGE_STATE_REQUEST = 6,
    LUNA_LINK_MESSAGE_STATE_SNAPSHOT = 7,
    LUNA_LINK_MESSAGE_ACTION_REQUEST = 8,
    LUNA_LINK_MESSAGE_ACTION_RESULT = 9,
    LUNA_LINK_MESSAGE_COVER_INFO_REQUEST = 10,
    LUNA_LINK_MESSAGE_COVER_INFO = 11,
    LUNA_LINK_MESSAGE_COVER_CHUNK_REQUEST = 12,
    LUNA_LINK_MESSAGE_COVER_CHUNK = 13,
    LUNA_LINK_MESSAGE_DEVICE_DIAGNOSTICS = 14,
} luna_link_message_type_t;

typedef enum {
    LUNA_USB_EVENT_DRIVER_READY,
    LUNA_USB_EVENT_ATTACHED,
    LUNA_USB_EVENT_CONNECTED,
    LUNA_USB_EVENT_DISCONNECTED,
    LUNA_USB_EVENT_HANDSHAKE,
    LUNA_USB_EVENT_PING,
    LUNA_USB_EVENT_ERROR,
} luna_usb_event_type_t;

typedef struct {
    luna_usb_event_type_t type;
    uint32_t request_id;
    const char *detail;
} luna_usb_event_t;

typedef void (*luna_usb_event_cb_t)(const luna_usb_event_t *event, void *context);

typedef struct {
    uint32_t cdc_opens;
    uint32_t handshakes;
    uint32_t exchanges_ok;
    uint32_t exchanges_failed;
    uint32_t exchanges_timed_out;
    uint32_t protocol_errors;
    uint32_t queue_drops;
    uint32_t tx_errors;
} luna_usb_stats_t;

/**
 * Start Luna Link over the native USB OTG CDC interface.
 *
 * This does not replace the UART console. The board's USB TO UART connector
 * remains the build, flash, and monitor path while the USB OTG connector is
 * used for Luna Link.
 */
esp_err_t luna_usb_start(luna_usb_event_cb_t callback, void *context);

/** Return true after the Windows host has opened the CDC interface (DTR set). */
bool luna_usb_is_connected(void);

/** Return true after the Windows agent completes the Luna Link handshake. */
bool luna_usb_is_ready(void);

/** Read cumulative diagnostics since this firmware boot. */
void luna_usb_get_stats(luna_usb_stats_t *stats);

/** Publish a runtime snapshot to the agent without waiting for an acknowledgement. */
esp_err_t luna_usb_send_diagnostics(bool wifi_online);

/**
 * Send one request and wait for its matching response frame.
 *
 * Only a small number of concurrent callers are supported. The response is
 * copied into the caller-owned buffer before this function returns.
 */
esp_err_t luna_usb_exchange(uint8_t request_type, uint8_t response_type,
                            const void *payload, size_t payload_length,
                            void *response, size_t response_capacity,
                            size_t *response_length, uint32_t timeout_ms);

/** Queue a diagnostic touch event for the connected Windows probe. */
esp_err_t luna_usb_send_touch_test(uint32_t touch_count);

#ifdef __cplusplus
}
#endif
