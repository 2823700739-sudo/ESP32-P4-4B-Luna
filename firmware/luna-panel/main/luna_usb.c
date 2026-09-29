// SPDX-License-Identifier: CC0-1.0

#include "luna_usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#define LUNA_USB_MAGIC "LUNA"
#define LUNA_USB_MAGIC_SIZE 4U
#define LUNA_USB_HEADER_SIZE 16U
#define LUNA_USB_CRC_SIZE 4U
#define LUNA_USB_RX_STREAM_SIZE 8192U
#define LUNA_USB_RX_CHUNK_SIZE 512U
#define LUNA_USB_EVENT_QUEUE_DEPTH 12U
#define LUNA_USB_TASK_STACK_SIZE 6144U
#define LUNA_USB_TASK_PRIORITY 5U
#define LUNA_USB_PENDING_COUNT 3U

typedef enum {
    LUNA_INTERNAL_RX_CHUNK,
    LUNA_INTERNAL_TX_MESSAGE,
    LUNA_INTERNAL_ATTACHED,
    LUNA_INTERNAL_DETACHED,
    LUNA_INTERNAL_LINE_STATE,
} luna_internal_event_type_t;

typedef struct {
    size_t length;
    uint8_t data[LUNA_USB_RX_CHUNK_SIZE];
} luna_rx_chunk_t;

typedef struct {
    uint8_t type;
    uint32_t request_id;
    size_t payload_length;
    uint8_t *payload;
} luna_tx_message_t;

typedef struct {
    bool active;
    uint8_t response_type;
    uint32_t request_id;
    uint8_t *response;
    size_t response_capacity;
    size_t response_length;
    esp_err_t result;
    SemaphoreHandle_t done;
} luna_pending_exchange_t;

typedef struct {
    luna_internal_event_type_t type;
    union {
        luna_rx_chunk_t rx;
        luna_tx_message_t tx;
        struct {
            bool dtr;
            bool rts;
        } line_state;
    } data;
} luna_internal_event_t;

static const char *TAG = "luna_usb";
static QueueHandle_t s_event_queue;
static luna_usb_event_cb_t s_event_callback;
static void *s_event_context;
static volatile bool s_started;
static volatile bool s_connected;
static volatile bool s_handshake_complete;
static uint8_t s_rx_stream[LUNA_USB_RX_STREAM_SIZE];
static size_t s_rx_stream_length;
static SemaphoreHandle_t s_pending_mutex;
static luna_pending_exchange_t s_pending[LUNA_USB_PENDING_COUNT];

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 16) & 0xFFU);
    data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t luna_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

static void notify_application(luna_usb_event_type_t type, uint32_t request_id,
                               const char *detail)
{
    if (s_event_callback == NULL) {
        return;
    }
    const luna_usb_event_t event = {
        .type = type,
        .request_id = request_id,
        .detail = detail,
    };
    s_event_callback(&event, s_event_context);
}

static bool queue_internal_event(const luna_internal_event_t *event)
{
    return s_event_queue != NULL && xQueueSend(s_event_queue, event, 0) == pdTRUE;
}

static void tinyusb_device_event_callback(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    luna_internal_event_t queued;
    if (event->id == TINYUSB_EVENT_ATTACHED) {
        queued.type = LUNA_INTERNAL_ATTACHED;
    } else if (event->id == TINYUSB_EVENT_DETACHED) {
        queued.type = LUNA_INTERNAL_DETACHED;
    } else {
        return;
    }
    if (!queue_internal_event(&queued)) {
        ESP_LOGW(TAG, "Dropping USB device event because the queue is full");
    }
}

static void cdc_line_state_callback(int interface, cdcacm_event_t *event)
{
    (void)interface;
    const luna_internal_event_t queued = {
        .type = LUNA_INTERNAL_LINE_STATE,
        .data.line_state = {
            .dtr = event->line_state_changed_data.dtr,
            .rts = event->line_state_changed_data.rts,
        },
    };
    if (!queue_internal_event(&queued)) {
        ESP_LOGW(TAG, "Dropping CDC line-state event because the queue is full");
    }
}

static void cdc_rx_callback(int interface, cdcacm_event_t *event)
{
    (void)event;
    while (true) {
        luna_internal_event_t queued = {
            .type = LUNA_INTERNAL_RX_CHUNK,
        };
        size_t received = 0;
        const esp_err_t result = tinyusb_cdcacm_read(
            interface, queued.data.rx.data, sizeof(queued.data.rx.data), &received);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "CDC read failed: %s", esp_err_to_name(result));
            break;
        }
        if (received == 0) {
            break;
        }
        queued.data.rx.length = received;
        if (!queue_internal_event(&queued)) {
            ESP_LOGW(TAG, "Dropping %u USB RX bytes because the queue is full",
                     (unsigned)received);
            break;
        }
        if (received < sizeof(queued.data.rx.data)) {
            break;
        }
    }
}

static esp_err_t write_message(const luna_tx_message_t *message)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (message->payload_length > LUNA_USB_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t frame_length =
        LUNA_USB_HEADER_SIZE + message->payload_length + LUNA_USB_CRC_SIZE;
    uint8_t *frame = malloc(frame_length);
    if (frame == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(frame, LUNA_USB_MAGIC, LUNA_USB_MAGIC_SIZE);
    frame[4] = LUNA_USB_PROTOCOL_VERSION;
    frame[5] = message->type;
    frame[6] = 0;
    frame[7] = 0;
    write_le32(frame + 8, message->request_id);
    write_le32(frame + 12, (uint32_t)message->payload_length);
    if (message->payload_length > 0) {
        memcpy(frame + LUNA_USB_HEADER_SIZE, message->payload, message->payload_length);
    }
    const size_t content_length = LUNA_USB_HEADER_SIZE + message->payload_length;
    write_le32(frame + content_length, luna_crc32(frame, content_length));

    const size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, frame, frame_length);
    if (queued != frame_length) {
        ESP_LOGW(TAG, "CDC TX queue accepted %u of %u bytes", (unsigned)queued,
                 (unsigned)frame_length);
        free(frame);
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t result =
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
    free(frame);
    return result;
}

static esp_err_t queue_message(uint8_t type, uint32_t request_id, const void *payload,
                               size_t payload_length)
{
    if (s_event_queue == NULL || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (payload_length > LUNA_USB_MAX_PAYLOAD || (payload_length > 0 && payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *payload_copy = NULL;
    if (payload_length > 0) {
        payload_copy = malloc(payload_length);
        if (payload_copy == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(payload_copy, payload, payload_length);
    }

    luna_internal_event_t event = {
        .type = LUNA_INTERNAL_TX_MESSAGE,
        .data.tx = {
            .type = type,
            .request_id = request_id,
            .payload_length = payload_length,
            .payload = payload_copy,
        },
    };
    if (!queue_internal_event(&event)) {
        free(payload_copy);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static bool complete_pending_response(uint8_t type, uint32_t request_id,
                                      const uint8_t *payload, size_t payload_length)
{
    if (s_pending_mutex == NULL ||
        xSemaphoreTake(s_pending_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool matched = false;
    for (size_t index = 0; index < LUNA_USB_PENDING_COUNT; ++index) {
        luna_pending_exchange_t *pending = &s_pending[index];
        if (!pending->active || pending->response_type != type ||
            pending->request_id != request_id) {
            continue;
        }
        if (payload_length > pending->response_capacity) {
            pending->result = ESP_ERR_INVALID_SIZE;
        } else {
            if (payload_length > 0) {
                memcpy(pending->response, payload, payload_length);
            }
            pending->response_length = payload_length;
            pending->result = ESP_OK;
        }
        xSemaphoreGive(pending->done);
        matched = true;
        break;
    }
    xSemaphoreGive(s_pending_mutex);
    return matched;
}

static void fail_pending_exchanges(esp_err_t result)
{
    if (s_pending_mutex == NULL ||
        xSemaphoreTake(s_pending_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    for (size_t index = 0; index < LUNA_USB_PENDING_COUNT; ++index) {
        luna_pending_exchange_t *pending = &s_pending[index];
        if (pending->active) {
            pending->result = result;
            xSemaphoreGive(pending->done);
        }
    }
    xSemaphoreGive(s_pending_mutex);
}

static void process_message(uint8_t type, uint32_t request_id, const uint8_t *payload,
                            size_t payload_length)
{
    switch (type) {
    case LUNA_LINK_MESSAGE_HELLO: {
        static const char response[] =
            "{\"device\":\"luna\",\"protocol\":1,\"firmware\":\"R2-USB\","
            "\"capabilities\":[\"ping\",\"touch_test\",\"state\",\"action\"]}";
        if (queue_message(LUNA_LINK_MESSAGE_HELLO_ACK, request_id, response,
                          sizeof(response) - 1) == ESP_OK) {
            s_handshake_complete = true;
            notify_application(LUNA_USB_EVENT_HANDSHAKE, request_id,
                               "Luna Link handshake complete");
        }
        break;
    }
    case LUNA_LINK_MESSAGE_PING:
        if (queue_message(LUNA_LINK_MESSAGE_PONG, request_id, payload, payload_length) == ESP_OK) {
            notify_application(LUNA_USB_EVENT_PING, request_id, "USB ping received");
        }
        break;
    case LUNA_LINK_MESSAGE_STATE_SNAPSHOT:
    case LUNA_LINK_MESSAGE_ACTION_RESULT:
    case LUNA_LINK_MESSAGE_COVER_INFO:
    case LUNA_LINK_MESSAGE_COVER_CHUNK:
        if (!complete_pending_response(type, request_id, payload, payload_length)) {
            ESP_LOGW(TAG, "Ignoring unmatched Luna Link response type=%u request=%lu",
                     (unsigned)type, (unsigned long)request_id);
        }
        break;
    default:
        ESP_LOGW(TAG, "Ignoring unsupported Luna Link message type %u", (unsigned)type);
        notify_application(LUNA_USB_EVENT_ERROR, request_id, "Unsupported USB message");
        break;
    }
}

static void drop_rx_prefix(size_t length)
{
    if (length >= s_rx_stream_length) {
        s_rx_stream_length = 0;
        return;
    }
    memmove(s_rx_stream, s_rx_stream + length, s_rx_stream_length - length);
    s_rx_stream_length -= length;
}

static void parse_rx_stream(void)
{
    while (s_rx_stream_length >= LUNA_USB_MAGIC_SIZE) {
        if (memcmp(s_rx_stream, LUNA_USB_MAGIC, LUNA_USB_MAGIC_SIZE) != 0) {
            drop_rx_prefix(1);
            continue;
        }
        if (s_rx_stream_length < LUNA_USB_HEADER_SIZE) {
            return;
        }

        const uint8_t version = s_rx_stream[4];
        const uint8_t type = s_rx_stream[5];
        const uint32_t request_id = read_le32(s_rx_stream + 8);
        const uint32_t payload_length = read_le32(s_rx_stream + 12);
        if (version != LUNA_USB_PROTOCOL_VERSION || payload_length > LUNA_USB_MAX_PAYLOAD) {
            ESP_LOGW(TAG, "Rejecting Luna Link header version=%u payload=%u", (unsigned)version,
                     (unsigned)payload_length);
            notify_application(LUNA_USB_EVENT_ERROR, request_id, "Invalid USB frame header");
            drop_rx_prefix(1);
            continue;
        }

        const size_t frame_length =
            LUNA_USB_HEADER_SIZE + (size_t)payload_length + LUNA_USB_CRC_SIZE;
        if (s_rx_stream_length < frame_length) {
            return;
        }
        const uint32_t expected_crc = read_le32(s_rx_stream + frame_length - LUNA_USB_CRC_SIZE);
        const uint32_t actual_crc =
            luna_crc32(s_rx_stream, frame_length - LUNA_USB_CRC_SIZE);
        if (expected_crc != actual_crc) {
            ESP_LOGW(TAG, "Rejecting Luna Link frame with invalid CRC");
            notify_application(LUNA_USB_EVENT_ERROR, request_id, "USB frame CRC mismatch");
            drop_rx_prefix(1);
            continue;
        }

        process_message(type, request_id, s_rx_stream + LUNA_USB_HEADER_SIZE, payload_length);
        drop_rx_prefix(frame_length);
    }
}

static void append_rx_chunk(const luna_rx_chunk_t *chunk)
{
    if (chunk->length > sizeof(s_rx_stream) - s_rx_stream_length) {
        ESP_LOGW(TAG, "Resetting USB RX parser after stream overflow");
        s_rx_stream_length = 0;
        notify_application(LUNA_USB_EVENT_ERROR, 0, "USB receive buffer overflow");
    }
    if (chunk->length > sizeof(s_rx_stream)) {
        return;
    }
    memcpy(s_rx_stream + s_rx_stream_length, chunk->data, chunk->length);
    s_rx_stream_length += chunk->length;
    parse_rx_stream();
}

static void luna_usb_task(void *arg)
{
    (void)arg;
    luna_internal_event_t event;
    while (xQueueReceive(s_event_queue, &event, portMAX_DELAY) == pdTRUE) {
        switch (event.type) {
        case LUNA_INTERNAL_RX_CHUNK:
            append_rx_chunk(&event.data.rx);
            break;
        case LUNA_INTERNAL_TX_MESSAGE: {
            const esp_err_t result = write_message(&event.data.tx);
            free(event.data.tx.payload);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "USB message send failed: %s", esp_err_to_name(result));
                notify_application(LUNA_USB_EVENT_ERROR, event.data.tx.request_id,
                                   "USB message send failed");
            }
            break;
        }
        case LUNA_INTERNAL_ATTACHED:
            notify_application(LUNA_USB_EVENT_ATTACHED, 0,
                               "USB host attached; waiting for CDC open");
            break;
        case LUNA_INTERNAL_DETACHED:
            s_connected = false;
            s_handshake_complete = false;
            s_rx_stream_length = 0;
            fail_pending_exchanges(ESP_ERR_INVALID_STATE);
            notify_application(LUNA_USB_EVENT_DISCONNECTED, 0, "USB host detached");
            break;
        case LUNA_INTERNAL_LINE_STATE: {
            const bool was_connected = s_connected;
            s_connected = event.data.line_state.dtr;
            ESP_LOGI(TAG, "CDC line state DTR=%d RTS=%d", event.data.line_state.dtr,
                     event.data.line_state.rts);
            if (s_connected && !was_connected) {
                notify_application(LUNA_USB_EVENT_CONNECTED, 0,
                                   "USB CDC open; waiting for Luna Link handshake");
            } else if (!s_connected && was_connected) {
                s_handshake_complete = false;
                s_rx_stream_length = 0;
                fail_pending_exchanges(ESP_ERR_INVALID_STATE);
                notify_application(LUNA_USB_EVENT_DISCONNECTED, 0, "USB CDC closed");
            }
            break;
        }
        }
    }
    vTaskDelete(NULL);
}

static void delete_pending_resources(void)
{
    for (size_t index = 0; index < LUNA_USB_PENDING_COUNT; ++index) {
        if (s_pending[index].done != NULL) {
            vSemaphoreDelete(s_pending[index].done);
            s_pending[index].done = NULL;
        }
    }
    if (s_pending_mutex != NULL) {
        vSemaphoreDelete(s_pending_mutex);
        s_pending_mutex = NULL;
    }
}

esp_err_t luna_usb_start(luna_usb_event_cb_t callback, void *context)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_event_callback = callback;
    s_event_context = context;
    s_event_queue = xQueueCreate(LUNA_USB_EVENT_QUEUE_DEPTH, sizeof(luna_internal_event_t));
    if (s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_pending_mutex = xSemaphoreCreateMutex();
    if (s_pending_mutex == NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0; index < LUNA_USB_PENDING_COUNT; ++index) {
        s_pending[index].done = xSemaphoreCreateBinary();
        if (s_pending[index].done == NULL) {
            delete_pending_resources();
            vQueueDelete(s_event_queue);
            s_event_queue = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    const tinyusb_config_t tinyusb_config =
        TINYUSB_DEFAULT_CONFIG(tinyusb_device_event_callback, NULL);
    esp_err_t result = tinyusb_driver_install(&tinyusb_config);
    if (result != ESP_OK) {
        delete_pending_resources();
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return result;
    }

    const tinyusb_config_cdcacm_t cdc_config = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = cdc_line_state_callback,
        .callback_line_coding_changed = NULL,
    };
    result = tinyusb_cdcacm_init(&cdc_config);
    if (result != ESP_OK) {
        tinyusb_driver_uninstall();
        delete_pending_resources();
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return result;
    }

    if (xTaskCreate(luna_usb_task, "luna_usb", LUNA_USB_TASK_STACK_SIZE, NULL,
                    LUNA_USB_TASK_PRIORITY, NULL) != pdPASS) {
        tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
        tinyusb_driver_uninstall();
        delete_pending_resources();
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    notify_application(LUNA_USB_EVENT_DRIVER_READY, 0, "USB CDC ready; connect the OTG port");
    ESP_LOGI(TAG, "Luna Link USB CDC initialized");
    return ESP_OK;
}

bool luna_usb_is_connected(void)
{
    return s_connected;
}

bool luna_usb_is_ready(void)
{
    return s_connected && s_handshake_complete;
}

esp_err_t luna_usb_exchange(uint8_t request_type, uint8_t response_type,
                            const void *payload, size_t payload_length,
                            void *response, size_t response_capacity,
                            size_t *response_length, uint32_t timeout_ms)
{
    if (!luna_usb_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (response == NULL || response_capacity == 0 || response_length == NULL ||
        timeout_ms == 0 || payload_length > LUNA_USB_MAX_PAYLOAD ||
        (payload_length > 0 && payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    luna_pending_exchange_t *pending = NULL;
    for (size_t index = 0; index < LUNA_USB_PENDING_COUNT; ++index) {
        if (!s_pending[index].active) {
            pending = &s_pending[index];
            break;
        }
    }
    if (pending == NULL) {
        xSemaphoreGive(s_pending_mutex);
        return ESP_ERR_NO_MEM;
    }

    while (xSemaphoreTake(pending->done, 0) == pdTRUE) {
    }
    uint32_t request_id = esp_random();
    if (request_id == 0) {
        request_id = 1;
    }
    pending->active = true;
    pending->response_type = response_type;
    pending->request_id = request_id;
    pending->response = response;
    pending->response_capacity = response_capacity;
    pending->response_length = 0;
    pending->result = ESP_ERR_TIMEOUT;
    xSemaphoreGive(s_pending_mutex);

    esp_err_t result = queue_message(request_type, request_id, payload, payload_length);
    if (result == ESP_OK &&
        xSemaphoreTake(pending->done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(s_pending_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (result == ESP_OK) {
        result = pending->result;
    }
    if (result == ESP_OK) {
        *response_length = pending->response_length;
    } else {
        *response_length = 0;
    }
    pending->active = false;
    pending->response = NULL;
    pending->response_capacity = 0;
    xSemaphoreGive(s_pending_mutex);
    return result;
}

esp_err_t luna_usb_send_touch_test(uint32_t touch_count)
{
    char payload[64];
    const int written =
        snprintf(payload, sizeof(payload), "{\"touch_count\":%lu}", (unsigned long)touch_count);
    if (written <= 0 || (size_t)written >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint32_t request_id = esp_random();
    if (request_id == 0) {
        request_id = 1;
    }
    return queue_message(LUNA_LINK_MESSAGE_TOUCH_TEST, request_id, payload, (size_t)written);
}
