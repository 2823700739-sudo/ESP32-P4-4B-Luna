// SPDX-License-Identifier: CC0-1.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUNA_USB_PROTOCOL_VERSION 1U

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

/** Queue a diagnostic touch event for the connected Windows probe. */
esp_err_t luna_usb_send_touch_test(uint32_t touch_count);

#ifdef __cplusplus
}
#endif
