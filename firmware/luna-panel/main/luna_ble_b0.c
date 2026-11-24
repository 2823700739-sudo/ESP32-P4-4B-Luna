// SPDX-License-Identifier: CC0-1.0
// Isolated diagnostic application. Not the five-card production firmware.
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_hosted.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_sm.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "luna_ble_frame.h"
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
#include "luna_ble_music.h"
#include "luna_dashboard.h"
#include "luna_music_ui.h"
#include "luna_time.h"
#include "luna_power.h"
#endif
#ifdef CONFIG_LUNA_BLE_B0_WIFI_TEST
#include "luna_ble_wifi.h"
#endif

#ifndef CONFIG_LUNA_BLE_B0
#error "B0 application requires the isolated BLE sdkconfig profile"
#endif

void ble_store_config_init(void);
static const char *TAG = "luna_ble_b0";
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x01,0x4c,0x8a,0x1b,0xd2,0x15,0x2b,0xbb,0x61,0x4b,0x7a,0x9e,0x01,0x00,0xa1,0xc4);
static const ble_uuid128_t rx_uuid = BLE_UUID128_INIT(
    0x01,0x4c,0x8a,0x1b,0xd2,0x15,0x2b,0xbb,0x61,0x4b,0x7a,0x9e,0x02,0x00,0xa1,0xc4);
static const ble_uuid128_t tx_uuid = BLE_UUID128_INIT(
    0x01,0x4c,0x8a,0x1b,0xd2,0x15,0x2b,0xbb,0x61,0x4b,0x7a,0x9e,0x03,0x00,0xa1,0xc4);
static const ble_uuid128_t ready_uuid = BLE_UUID128_INIT(
    0x01,0x4c,0x8a,0x1b,0xd2,0x15,0x2b,0xbb,0x61,0x4b,0x7a,0x9e,0x04,0x00,0xa1,0xc4);
static uint8_t own_addr_type;
static uint16_t tx_handle;
static atomic_uint conn_handle = BLE_HS_CONN_HANDLE_NONE, generation, pair_code, pair_generation;
static atomic_bool subscribed, pairing;
static atomic_int pair_answer = -1;
static atomic_uint answer_generation;
static uint16_t pair_conn = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint pair_started;
static atomic_int diagnostic_enc = -1, diagnostic_confirm = -1, diagnostic_disconnect = -1;
static atomic_uint diagnostic_security;
static atomic_uint session_payload = 20;
static TaskHandle_t tx_task_handle;
static uint16_t last_request_id;
static char boot_id[17], session[65];
static luna_ble_rx_t receiver;
static struct ble_npl_event pair_event;
static struct ble_npl_callout adv_retry;
static struct ble_npl_event recovery_event;
static ble_addr_t recovery_peer;
static atomic_bool recovery_pending;
static atomic_uint recovery_generation, recovery_answer_generation, recovery_started;
static lv_obj_t *recovery_button;
static lv_obj_t *status_label, *pair_label, *accept_button, *reject_button, *clock_label, *diagnostic_label;
static QueueHandle_t status_queue, tx_queue;
static lv_obj_t *diagnostic_page;

typedef struct { char text[140]; } ui_status_t;
typedef struct { uint16_t conn, id, len; uint32_t generation; char data[512]; } tx_item_t;

static void status(const char *text)
{
    ui_status_t item = {0};
    snprintf(item.text, sizeof(item.text), "%s", text);
    xQueueOverwrite(status_queue, &item);
    ESP_LOGI(TAG, "%s", text);
}

static bool secure(uint16_t conn)
{
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(conn, &desc) == 0 && desc.sec_state.encrypted &&
           desc.sec_state.authenticated && desc.sec_state.bonded && desc.sec_state.key_size == 16;
}

static uint16_t mtu_payload(uint16_t conn)
{
    uint16_t mtu = ble_att_mtu(conn);
    return mtu < 23 ? 20 : mtu > 515 ? 512 : mtu - 3;
}

static int queue_reply(uint16_t conn, uint16_t id, const char *json)
{
    tx_item_t item = {.conn=conn, .id=id, .generation=atomic_load(&generation)};
    size_t len = strlen(json);
    if (!len || len >= sizeof(item.data)) return BLE_ATT_ERR_INSUFFICIENT_RES;
    memcpy(item.data, json, len); item.len = len;
    return xQueueSend(tx_queue, &item, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static bool shallow_json(const uint8_t *data, size_t len)
{
    // Bound parser recursion before entering cJSON on the NimBLE task stack.
    unsigned depth = 0; bool quoted = false, escaped = false;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; depth--; }
    }
    return !quoted && depth == 0;
}

static int handle_message(uint16_t conn, uint16_t id, const uint8_t *data, size_t len)
{
    // Embedded NUL is forbidden; JSON parser must consume the entire message.
    if (memchr(data, 0, len) || !shallow_json(data, len)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts((const char *)data, len + 1, &end, true);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return BLE_ATT_ERR_UNLIKELY; }
    for (const cJSON *a = root->child; a; a = a->next) {
        for (const cJSON *b = a->next; b; b = b->next) {
            if (strcmp(a->string, b->string) == 0) { cJSON_Delete(root); return BLE_ATT_ERR_UNLIKELY; }
        }
    }
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *sid = cJSON_GetObjectItemCaseSensitive(root, "session");
    int result = BLE_ATT_ERR_UNLIKELY;
    char reply[512];
    if (!cJSON_IsNumber(v) || v->valuedouble != 1 || !cJSON_IsString(type) ||
        !cJSON_IsString(sid) || !sid->valuestring[0] || strlen(sid->valuestring) > 64) goto done;
    // Session IDs have a small, unambiguous alphabet; never interpolate arbitrary JSON.
    for (const char *p = sid->valuestring; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') || *p == '-')) goto done;
    }
    if (strcmp(type->valuestring, "hello") == 0 && !session[0]) {
        cJSON *offer = cJSON_GetObjectItemCaseSensitive(root, "att_payload");
        unsigned payload_limit = 20; // Older clients remain on the safe small path.
        if (offer) {
            if (!cJSON_IsNumber(offer) || !isfinite(offer->valuedouble) ||
                floor(offer->valuedouble) != offer->valuedouble ||
                offer->valuedouble < 20 || offer->valuedouble > 512) goto done;
            payload_limit = (unsigned)offer->valuedouble;
            if (payload_limit > mtu_payload(conn)) payload_limit = mtu_payload(conn);
        }
        atomic_store(&session_payload, payload_limit);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        const char *mode = "b1-music", *features = "\"diagnostic\",\"mtu_payload\",\"music\",\"time_status\",\"dashboard\"";
#else
        const char *mode = "b0-diagnostic", *features = "\"diagnostic\",\"mtu_payload\"";
#endif
        snprintf(reply, sizeof(reply), "{\"v\":1,\"type\":\"hello\",\"name\":\"Luna\","
                 "\"boot_id\":\"%s\",\"session\":\"%s\",\"max_message\":4096,"
                 "\"att_payload\":%u,\"mode\":\"%s\",\"features\":[%s]}",
                 boot_id, sid->valuestring, payload_limit, mode, features);
        result = queue_reply(conn, id, reply);
        if (!result) snprintf(session, sizeof(session), "%s", sid->valuestring);
    } else if (session[0] && strcmp(session, sid->valuestring) == 0) {
        if (strcmp(type->valuestring, "ping") == 0) {
            snprintf(reply, sizeof(reply), "{\"v\":1,\"type\":\"pong\",\"session\":\"%s\","
                     "\"boot_id\":\"%s\",\"mtu\":%u}", session, boot_id, ble_att_mtu(conn));
            result = queue_reply(conn, id, reply);
        } else if (strcmp(type->valuestring, "diagnostic") == 0) {
            const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
            char network[128] = {0};
#ifdef CONFIG_LUNA_BLE_B0_WIFI_TEST
            luna_ble_wifi_status_t wifi = luna_ble_wifi_status();
            snprintf(network, sizeof(network), ",\"wifi_online\":%s,\"weather_configured\":%s,\"weather_available\":%s,\"weather_cached\":%s",
                     wifi.online ? "true" : "false", wifi.configured ? "true" : "false",
                     wifi.available ? "true" : "false", wifi.cached ? "true" : "false");
#endif
            snprintf(reply, sizeof(reply), "{\"v\":1,\"type\":\"diagnostic\",\"session\":\"%s\","
                     "\"boot_id\":\"%s\",\"mtu\":%u,\"internal_free_bytes\":%u,\"internal_min_bytes\":%u,"
                     "\"psram_free_bytes\":%u,\"psram_min_bytes\":%u,\"host_stack_free_bytes\":%u,\"tx_stack_free_bytes\":%u%s}", session, boot_id, ble_att_mtu(conn),
                     (unsigned)heap_caps_get_free_size(caps), (unsigned)heap_caps_get_minimum_free_size(caps),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)uxTaskGetStackHighWaterMark(tx_task_handle), network);
            result = queue_reply(conn, id, reply);
        } else if (strcmp(type->valuestring, "time_sync") == 0) {
            cJSON *epoch = cJSON_GetObjectItemCaseSensitive(root, "epoch_ms");
            cJSON *offset = cJSON_GetObjectItemCaseSensitive(root, "timezone_offset_minutes");
            if (!cJSON_IsNumber(epoch) || !isfinite(epoch->valuedouble) ||
                epoch->valuedouble < 946684800000.0 || epoch->valuedouble >= 4102444800000.0 ||
                floor(epoch->valuedouble) != epoch->valuedouble || !cJSON_IsNumber(offset) ||
                offset->valuedouble < -840 || offset->valuedouble > 840 ||
                floor(offset->valuedouble) != offset->valuedouble) goto done;
            int64_t ms = (int64_t)epoch->valuedouble;
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
            bool accepted = luna_time_accept_ble(ms);
#else
            struct timeval tv = {.tv_sec=ms/1000, .tv_usec=(ms%1000)*1000};
            bool accepted = settimeofday(&tv, NULL) == 0;
#endif
            snprintf(reply, sizeof(reply), "{\"v\":1,\"type\":\"time_sync_result\","
                     "\"session\":\"%s\",\"accepted\":%s,\"epoch_ms\":%.0f,"
                     "\"timezone_policy\":\"CST-8\"}", session, accepted ? "true" : "false", epoch->valuedouble);
            result = queue_reply(conn, id, reply);
        }
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        else if (!strcmp(type->valuestring, "time_status")) {
            luna_time_policy_t time_state = luna_time_snapshot();
            struct timeval tv; gettimeofday(&tv, NULL);
            int64_t age = time_state.valid ? (esp_timer_get_time() - time_state.last_sync_us) / 1000 : -1;
            snprintf(reply, sizeof(reply), "{\"v\":1,\"type\":\"time_status\",\"session\":\"%s\",\"boot_id\":\"%s\","
                     "\"valid\":%s,\"source\":\"%s\",\"last_sync_age_ms\":%" PRId64 ",\"epoch_ms\":%" PRId64 "}",
                     session, boot_id, time_state.valid ? "true" : "false", luna_time_source_name(time_state.source),
                     age, (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
            result = queue_reply(conn, id, reply);
        }
        else if (!strcmp(type->valuestring, "dashboard_snapshot")) {
            result = luna_dashboard_message(root, session, boot_id, reply, sizeof(reply));
            if (!result) result = queue_reply(conn, id, reply);
        }
        else if (!strcmp(type->valuestring, "state_snapshot") || !strcmp(type->valuestring, "action_result")) {
            result = luna_music_message(type->valuestring, root, session, reply, sizeof(reply));
            if (!result) result = queue_reply(conn, id, reply);
        }
#endif
    }
done:
    cJSON_Delete(root);
    return result;
}

static int access_rx(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (!secure(conn)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (!atomic_load(&subscribed)) return BLE_ATT_ERR_UNLIKELY;
    uint8_t packet[512]; uint16_t len;
    if (OS_MBUF_PKTLEN(ctxt->om) > sizeof(packet)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, packet, sizeof(packet), &len)) return BLE_ATT_ERR_UNLIKELY;
    uint16_t id = len >= 4 ? (uint16_t)packet[2] | ((uint16_t)packet[3] << 8) : 0;
    int rc = luna_ble_frame_feed(&receiver, packet, len, (uint32_t)(esp_timer_get_time()/1000));
    if (rc < 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (!rc) return 0;
    // B0 only: monotonically increasing IDs, no replay even after unknown outcomes.
    if (id <= last_request_id) return BLE_ATT_ERR_UNLIKELY;
    last_request_id = id;
    return handle_message(conn, id, receiver.data, receiver.total);
}

static int access_tx(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)ctxt; (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int access_ready(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    // Only this connection's security readiness; no application data or identity.
    // RX and TX retain their independent encryption/authentication/bond checks.
    uint8_t ready = secure(conn) ? 1 : 0;
    return os_mbuf_append(ctxt->om, &ready, sizeof(ready)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static const struct ble_gatt_svc_def services[] = {{
    .type=BLE_GATT_SVC_TYPE_PRIMARY, .uuid=&service_uuid.u,
    .characteristics=(struct ble_gatt_chr_def[]) {
        {.uuid=&rx_uuid.u, .access_cb=access_rx,
         .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&tx_uuid.u, .access_cb=access_tx, .val_handle=&tx_handle, .flags=BLE_GATT_CHR_F_NOTIFY},
        {.uuid=&ready_uuid.u, .access_cb=access_ready, .flags=BLE_GATT_CHR_F_READ},
        {0}
    }
}, {0}};

static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void)
{
    if(!ble_hs_synced()||atomic_load(&conn_handle)!=BLE_HS_CONN_HANDLE_NONE||ble_gap_adv_active())return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)"Luna"; fields.name_len = 4; fields.name_is_complete = 1;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid; fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    struct ble_gap_adv_params params = {.conn_mode=BLE_GAP_CONN_MODE_UND, .disc_mode=BLE_GAP_DISC_MODE_GEN};
    int rc = ble_gap_adv_set_fields(&fields);
    if (!rc) rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if(rc){
        ESP_LOGW(TAG,"BLE_ADV_START rc=%d; deferred retry; internal_free=%u largest=%u",rc,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        // CONNECT failure can arrive before NimBLE releases its connection
        // slot. Retry on the host queue after the current GAP callback returns.
        ble_npl_callout_reset(&adv_retry,ble_npl_time_ms_to_ticks32(1000));
        status("BLE advertising retry pending; bonds unchanged");
    }else{
        ble_npl_callout_stop(&adv_retry);
        ESP_LOGI(TAG,"BLE_ADV_START rc=0");
        status("Luna: waiting for Windows BLE connection");
    }
}
static void retry_advertise(struct ble_npl_event *event)
{(void)event;advertise();}

static void apply_bond_recovery(struct ble_npl_event *event)
{
    (void)event;
    if (!atomic_load(&recovery_pending) || atomic_load(&recovery_answer_generation) != atomic_load(&recovery_generation) ||
        (uint32_t)(esp_timer_get_time()/1000 - atomic_load(&recovery_started)) >= 60000) return;
    // Physical confirmation applies only to the peer that requested re-pairing.
    // Never clear all bonds or erase NVS, and never target a different connection.
    uint16_t active = atomic_load(&conn_handle);
    struct ble_gap_conn_desc desc;
    if (active != BLE_HS_CONN_HANDLE_NONE &&
        (ble_gap_conn_find(active, &desc) || ble_addr_cmp(&desc.peer_id_addr, &recovery_peer))) {
        status("Old bond reset blocked: a different peer is connected");
        return;
    }
    bool advertising = ble_gap_adv_active();
    if (advertising && ble_gap_adv_stop()) { status("Old bond reset blocked: cannot stop advertising"); return; }
    int rc = ble_gap_unpair(&recovery_peer);
    ESP_LOGI(TAG, "BOND_RECOVERY physical_confirmation=1 target=repeat_pairing_peer rc=%d", rc);
    if (!rc) atomic_store(&recovery_pending, false);
    if (active == BLE_HS_CONN_HANDLE_NONE) advertise();
    status(rc ? "Old bond reset failed; other bonds unchanged" : "Old PC bond removed: add Luna in Windows again");
}

static void apply_pair_answer(struct ble_npl_event *event)
{
    (void)event;
    int answer = atomic_exchange(&pair_answer, -1);
    if (answer < 0 || !atomic_load(&pairing) ||
        atomic_load(&answer_generation) != atomic_load(&pair_generation)) return;
    struct ble_sm_io io = {.action=BLE_SM_IOACT_NUMCMP, .numcmp_accept=answer};
    int rc = ble_sm_inject_io(pair_conn, &io);
    atomic_store(&diagnostic_confirm, rc);
    ESP_LOGI(TAG, "PAIR_CONFIRM accepted=%d rc=%d elapsed_ms=%" PRIu32,
             answer, rc, (uint32_t)(esp_timer_get_time()/1000) - atomic_load(&pair_started));
    atomic_store(&pairing, false);
    status(rc ? "Pairing answer failed" : answer ? "Pairing confirmed on Luna" : "Pairing rejected on Luna");
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        ESP_LOGI(TAG, "BLE_CONNECT status=%d", event->connect.status);
        if (event->connect.status) { advertise(); break; }
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        luna_music_reset_session();
        luna_dashboard_disconnect();
#endif
        atomic_store(&diagnostic_enc, -1); atomic_store(&diagnostic_confirm, -1);
        atomic_store(&diagnostic_disconnect, -1); atomic_store(&diagnostic_security, 0);
        atomic_fetch_add(&generation, 1);
        atomic_store(&conn_handle, event->connect.conn_handle);
        ble_npl_callout_stop(&adv_retry);
        atomic_store(&session_payload, 20);
        status("Connected: securing link; confirm BOTH screens if asked");
        // A new connection is not automatically an encrypted connection. Ask
        // the central to restore encryption from the existing bond, or begin
        // human-confirmed numerical comparison for an unpaired peer.
        int rc = ble_gap_security_initiate(event->connect.conn_handle);
        ESP_LOGI(TAG, "BLE_SECURITY_START rc=%d", rc);
        if (rc && rc != BLE_HS_EALREADY) {
            status("Security startup failed; application data remains blocked");
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_AUTH_FAIL);
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        luna_music_reset_session();
        luna_dashboard_disconnect();
#endif
        atomic_store(&diagnostic_disconnect, event->disconnect.reason);
        ESP_LOGW(TAG, "BLE_DISCONNECT reason=%d (0x%x)", event->disconnect.reason, event->disconnect.reason);
        atomic_fetch_add(&generation, 1);
        atomic_store(&conn_handle, BLE_HS_CONN_HANDLE_NONE);
        atomic_store(&subscribed, false); atomic_store(&pairing, false);
        pair_conn = BLE_HS_CONN_HANDLE_NONE; session[0] = 0; last_request_id = 0;
        atomic_store(&session_payload, 20);
        luna_ble_frame_reset(&receiver);
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == tx_handle) atomic_store(&subscribed, event->subscribe.cur_notify);
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        // Log only the action, never the comparison number, keys, or addresses.
        ESP_LOGI(TAG, "PAIR_METHOD action=%u", (unsigned)event->passkey.params.action);
        if (event->passkey.params.action != BLE_SM_IOACT_NUMCMP) {
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
            status("Unsupported pairing method: numerical comparison required");
            break;
        }
        pair_conn = event->passkey.conn_handle;
        atomic_store(&pair_started, (uint32_t)(esp_timer_get_time()/1000));
        atomic_store(&pair_answer, -1);
        atomic_store(&pair_generation, atomic_load(&generation));
        atomic_store(&pair_code, event->passkey.params.numcmp);
        atomic_store(&pairing, true);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc = {0};
        int find_rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        atomic_store(&diagnostic_enc, event->enc_change.status);
        unsigned flags = find_rc ? 0 : (desc.sec_state.encrypted | (desc.sec_state.authenticated << 1) |
                         (desc.sec_state.bonded << 2) | (desc.sec_state.key_size << 8));
        atomic_store(&diagnostic_security, flags);
        if (!event->enc_change.status && secure(event->enc_change.conn_handle)) atomic_store(&recovery_pending, false);
        ESP_LOGI(TAG, "BLE_ENC status=%d (0x%x) find=%d encrypted=%u authenticated=%u bonded=%u key_size=%u",
                 event->enc_change.status, event->enc_change.status, find_rc,
                 (unsigned)desc.sec_state.encrypted, (unsigned)desc.sec_state.authenticated,
                 (unsigned)desc.sec_state.bonded, (unsigned)desc.sec_state.key_size);
        status(!event->enc_change.status && secure(event->enc_change.conn_handle)
               ? "Paired + encrypted: ready for hello / ping / time_sync"
               : event->enc_change.status ? "Pairing failed; see ENC / disconnect codes below"
               : "Link security incomplete; application data remains blocked");
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (!ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) && !secure(event->repeat_pairing.conn_handle)) {
            recovery_peer = desc.peer_id_addr;
            atomic_fetch_add(&recovery_generation, 1);
            atomic_store(&recovery_started, (uint32_t)(esp_timer_get_time()/1000));
            atomic_store(&recovery_pending, true);
            status("Old bond exists: use panel reset ONLY if PC forgot Luna");
        }
        ESP_LOGW(TAG, "BLE_REPEAT_PAIRING ignored; physical reset required; existing bond preserved");
        // A remote request can offer recovery, never approve or delete a bond.
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_addr_type)) {
        status("BLE address initialization failed"); return;
    }
    advertise();
}
static void on_reset(int reason)
{
    ble_npl_callout_stop(&adv_retry);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    luna_music_reset_session();
    luna_dashboard_disconnect();
#endif
    atomic_fetch_add(&generation, 1); atomic_store(&conn_handle, BLE_HS_CONN_HANDLE_NONE);
    atomic_store(&subscribed, false); atomic_store(&pairing, false);
    pair_conn = BLE_HS_CONN_HANDLE_NONE; session[0] = 0; last_request_id = 0;
    atomic_store(&session_payload, 20);
    luna_ble_frame_reset(&receiver);
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }

static void tx_task(void *arg)
{
    (void)arg; tx_item_t item;
    while (xQueueReceive(tx_queue, &item, portMAX_DELAY) == pdTRUE) {
        for (size_t offset = 0; offset < item.len;) {
            if (item.generation != atomic_load(&generation) || item.conn != atomic_load(&conn_handle) ||
                !atomic_load(&subscribed) || !secure(item.conn)) break;
            // Both peers' negotiated cap AND this connection's actual ATT MTU.
            uint8_t packet[512];
            size_t limit = atomic_load(&session_payload);
            if (limit > mtu_payload(item.conn)) limit = mtu_payload(item.conn);
            size_t n = luna_ble_frame_encode(packet, sizeof(packet), (uint8_t *)item.data,
                                            item.len, item.id, offset, limit);
            if (!n) { status("Invalid negotiated fragment limit; request outcome unknown"); break; }
            struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, n);
            int rc = om ? ble_gatts_notify_custom(item.conn, tx_handle, om) : BLE_HS_ENOMEM;
            if (rc) { status("Notify failed: request outcome unknown; do not replay"); break; }
            offset += n - LUNA_BLE_HEADER_SIZE;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void pairing_click(lv_event_t *event)
{
    if (!atomic_load(&pairing) || atomic_load(&pair_answer) >= 0) return;
    atomic_store(&answer_generation, atomic_load(&pair_generation));
    atomic_store(&pair_answer, (int)(intptr_t)lv_event_get_user_data(event));
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &pair_event);
}

static void recovery_click(lv_event_t *event)
{
    (void)event;
    if (!atomic_load(&recovery_pending) || atomic_load(&pairing)) return;
    atomic_store(&recovery_answer_generation, atomic_load(&recovery_generation));
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &recovery_event);
}

static void ui_timer(lv_timer_t *timer)
{
    (void)timer; ui_status_t item;
    if (xQueueReceive(status_queue, &item, 0)) lv_label_set_text(status_label, item.text);
    bool pending = atomic_load(&pairing);
    if (atomic_load(&recovery_pending) &&
        (uint32_t)(esp_timer_get_time()/1000 - atomic_load(&recovery_started)) >= 60000) atomic_store(&recovery_pending, false);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    lv_obj_set_flag(diagnostic_page, LV_OBJ_FLAG_HIDDEN, !pending && !atomic_load(&recovery_pending));
    // Do not allocate/repaint hidden diagnostic text ten times per second.
    if(!pending && !atomic_load(&recovery_pending))return;
#endif
    lv_obj_set_flag(recovery_button, LV_OBJ_FLAG_HIDDEN, pending || !atomic_load(&recovery_pending));
    if (pending && (uint32_t)(esp_timer_get_time()/1000 - atomic_load(&pair_started)) >= 30000 &&
        atomic_load(&pair_answer) < 0) {
        ESP_LOGW(TAG, "PAIR_TIMEOUT: no Luna confirmation within 30 seconds");
        atomic_store(&answer_generation, atomic_load(&pair_generation)); atomic_store(&pair_answer, 0);
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &pair_event);
    }
    lv_obj_set_flag(accept_button, LV_OBJ_FLAG_HIDDEN, !pending);
    lv_obj_set_flag(reject_button, LV_OBJ_FLAG_HIDDEN, !pending);
    if (pending) lv_label_set_text_fmt(pair_label, "Compare with Windows:\n%06u\nConfirm on BOTH screens", atomic_load(&pair_code));
    else lv_label_set_text(pair_label, "B0 diagnostic only\nNo music / covers / USB CDC");
    unsigned security = atomic_load(&diagnostic_security);
    lv_label_set_text_fmt(diagnostic_label, "Confirm rc: %d   ENC: %d   Disconnect: %d\nEncrypted: %u   Auth: %u   Bond: %u   Key: %u",
                         atomic_load(&diagnostic_confirm), atomic_load(&diagnostic_enc), atomic_load(&diagnostic_disconnect),
                         security & 1, (security >> 1) & 1, (security >> 2) & 1, security >> 8);
    time_t now = time(NULL);
    if (now >= 946684800) {
        struct tm tm; localtime_r(&now, &tm); char text[48];
        strftime(text, sizeof(text), "%Y-%m-%d  %H:%M:%S  CST", &tm);
        lv_label_set_text(clock_label, text);
    } else lv_label_set_text(clock_label, "Waiting for BLE time_sync");
}

static void ui_start(void)
{
    bsp_display_cfg_t cfg = {.lv_adapter_cfg=ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation=ESP_LV_ADAPTER_ROTATE_0, .tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL};
    cfg.lv_adapter_cfg.task_stack_size = 8192;
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    // Render dirty rectangles directly into two panel framebuffers. LVGL
    // synchronizes the buffers; bypass the triple-partial tile-copy path while
    // investigating stale clock pixels on the physical LCD. No full-frame loop.
    cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT;
    cfg.lv_adapter_cfg.task_stack_size = 16384;
    cfg.lv_adapter_cfg.task_max_delay_ms = 200;
#endif
    ESP_ERROR_CHECK(bsp_display_start_with_config(&cfg) ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "Display tear avoidance mode=%d (B1 double-direct=3)", cfg.tear_avoid_mode);
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_ERROR_CHECK(bsp_display_lock(pdMS_TO_TICKS(2000)) ? ESP_OK : ESP_ERR_TIMEOUT);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xf1f5fb), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_24, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    luna_music_ui_start(screen);
#endif
    diagnostic_page = lv_obj_create(screen);
    lv_obj_set_size(diagnostic_page, 720, 720); lv_obj_set_pos(diagnostic_page, 0, 0);
    lv_obj_set_style_bg_color(diagnostic_page, lv_color_hex(0x101827), 0);
    lv_obj_set_style_border_width(diagnostic_page, 0, 0); lv_obj_set_style_pad_all(diagnostic_page, 0, 0);
    lv_obj_remove_flag(diagnostic_page, LV_OBJ_FLAG_SCROLLABLE);
    screen = diagnostic_page;
    lv_obj_t *title = lv_label_create(screen); lv_label_set_text(title, "Luna / BLE B0");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);
    status_label = lv_label_create(screen); lv_obj_set_width(status_label, 600);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 122);
    pair_label = lv_label_create(screen); lv_obj_align(pair_label, LV_ALIGN_CENTER, 0, -28);
    accept_button = lv_button_create(screen); lv_obj_set_size(accept_button, 230, 70);
    lv_obj_align(accept_button, LV_ALIGN_CENTER, -135, 100);
    lv_obj_add_event_cb(accept_button, pairing_click, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *label = lv_label_create(accept_button); lv_label_set_text(label, "Numbers match"); lv_obj_center(label);
    reject_button = lv_button_create(screen); lv_obj_set_size(reject_button, 230, 70);
    lv_obj_align(reject_button, LV_ALIGN_CENTER, 135, 100);
    lv_obj_add_event_cb(reject_button, pairing_click, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    label = lv_label_create(reject_button); lv_label_set_text(label, "Reject"); lv_obj_center(label);
    recovery_button = lv_button_create(screen); lv_obj_set_size(recovery_button, 490, 70);
    lv_obj_align(recovery_button, LV_ALIGN_CENTER, 0, 100);
    lv_obj_add_event_cb(recovery_button, recovery_click, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(recovery_button); lv_label_set_text(label, "Forget old PC bond"); lv_obj_center(label);
    diagnostic_label = lv_label_create(screen); lv_obj_set_width(diagnostic_label, 640);
    lv_obj_set_style_text_font(diagnostic_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(diagnostic_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(diagnostic_label, LV_ALIGN_BOTTOM_MID, 0, -125);
    clock_label = lv_label_create(screen); lv_obj_align(clock_label, LV_ALIGN_BOTTOM_MID, 0, -65);
    lv_timer_create(ui_timer, 100, NULL); ui_timer(NULL);
    bsp_display_unlock();
}

void app_main(void)
{
    setenv("TZ", "CST-8", 1); tzset();
    status_queue = xQueueCreate(1, sizeof(ui_status_t)); tx_queue = xQueueCreate(2, sizeof(tx_item_t));
    ESP_ERROR_CHECK(status_queue && tx_queue ? ESP_OK : ESP_ERR_NO_MEM);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    ESP_ERROR_CHECK(luna_music_init());
    ESP_ERROR_CHECK(luna_dashboard_init());
    ESP_ERROR_CHECK(luna_time_init());
    esp_err_t power_rc=luna_power_init();
    if(power_rc!=ESP_OK)ESP_LOGW(TAG,"Standby CPU profile unavailable rc=%d; continue at existing frequency",power_rc);
#endif
    ui_start(); status("Checking existing C6 controller (no C6 flash)");
#ifdef CONFIG_BT_NIMBLE_CRYPTO_STACK_MBEDTLS
    ESP_LOGI(TAG, "SMP crypto backend: mbedTLS; SC/MITM and key-size checks unchanged");
#else
    ESP_LOGI(TAG, "SMP crypto backend: TinyCrypt");
#endif
    esp_err_t rc = nvs_flash_init();
    // Preserve NVS/bonds; do not erase the user's data to recover init errors.
    if (rc != ESP_OK) { status("NVS init failed; preserve storage and stop BLE test"); return; }
    esp_hosted_connect_to_slave();
    esp_hosted_coprocessor_fwver_t version;
    esp_err_t version_rc = esp_hosted_get_coprocessor_fwversion(&version);
    if (version_rc == ESP_OK)
        ESP_LOGI(TAG, "C6 firmware: %" PRIu32 ".%" PRIu32 ".%" PRIu32, version.major1, version.minor1, version.patch1);
    else ESP_LOGW(TAG, "C6 version RPC failed: %d (version unknown)", (int)version_rc);
    rc = esp_hosted_bt_controller_init();
    ESP_LOGI(TAG, "C6 controller init RPC: %d", (int)rc);
#ifdef CONFIG_LUNA_BLE_B0_LEGACY_HCI_PROBE
    // Hosted <2.5.2 auto-enables the controller. Probe the existing HCI only;
    // never treat an RPC failure as success or weaken the paired GATT gates.
    // Unknown version is not asserted to be old: on_sync is the real gate.
    if (rc != ESP_OK && version_rc != ESP_OK) {
        status("Legacy compatibility probe: waiting for actual HCI synchronization");
    } else
#endif
    {
        if (rc == ESP_OK) {
            rc = esp_hosted_bt_controller_enable();
            ESP_LOGI(TAG, "C6 controller enable RPC: %d", (int)rc);
        }
        if (rc != ESP_OK) { status("Existing C6 BLE controller unavailable; stop, do not flash C6 automatically"); return; }
    }
    rc = nimble_port_init();
    if (rc != ESP_OK) { status("NimBLE initialization failed"); return; }
    snprintf(boot_id, sizeof(boot_id), "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    luna_music_set_boot_id(boot_id);
#endif
    ble_hs_cfg.reset_cb=on_reset; ble_hs_cfg.sync_cb=on_sync;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding=1; ble_hs_cfg.sm_mitm=1; ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init(); ble_svc_gatt_init(); ble_store_config_init();
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set("Luna") == 0 ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services) == 0 ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ble_gatts_add_svcs(services) == 0 ? ESP_OK : ESP_FAIL);
    ble_npl_event_init(&pair_event, apply_pair_answer, NULL);
    ESP_ERROR_CHECK(ble_npl_callout_init(&adv_retry,nimble_port_get_dflt_eventq(),retry_advertise,NULL)==0?ESP_OK:ESP_FAIL);
    ble_npl_event_init(&recovery_event, apply_bond_recovery, NULL);
    ESP_ERROR_CHECK(xTaskCreate(tx_task, "luna_ble_tx", 4096, NULL, 4, &tx_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    nimble_port_freertos_init(host_task);
#ifdef CONFIG_LUNA_BLE_B0_WIFI_TEST
    esp_err_t wifi_rc = luna_ble_wifi_start();
    ESP_LOGI(TAG, "Optional Wi-Fi/weather coexistence startup rc=%d", (int)wifi_rc);
#endif
}
