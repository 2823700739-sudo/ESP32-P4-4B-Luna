// Bounded B1 action FIFO. No actions survive a connection/session boundary.
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "esp_timer.h"
#include "luna_ble_music.h"

#define ACTION_CAPACITY 8
#define ACTION_TIMEOUT_US 6000000LL
typedef struct { char id[26], action[24]; int value; int64_t created_us; } action_t;
static SemaphoreHandle_t mutex;
static luna_music_state_t state;
static action_t actions[ACTION_CAPACITY];
static unsigned head, count;
static uint32_t serial;
static char boot[17], latest_id[26];
static bool desired_playing, playback_override;
static unsigned desired_volume;
static bool volume_override;
static int64_t pending_until, override_until, volume_until;

static bool lock(void) { return xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) == pdTRUE; }
static void unlock(void) { xSemaphoreGive(mutex); }
static bool boolean(const cJSON *o, const char *key, bool *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsBool(v)) return false;
    *out = cJSON_IsTrue(v); return true;
}
static bool text(const cJSON *o, const char *key, char *out, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) return false;
    strlcpy(out, v->valuestring, cap); return true;
}

esp_err_t luna_music_init(void)
{
    mutex = xSemaphoreCreateMutex();
    return mutex ? ESP_OK : ESP_ERR_NO_MEM;
}
void luna_music_set_boot_id(const char *id)
{
    if (lock()) { strlcpy(boot, id, sizeof(boot)); unlock(); }
}
void luna_music_reset_session(void)
{
    // NimBLE owns reset/message calls; bounded wait is sufficient for UI reads.
    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(&state, 0, sizeof(state)); head = count = 0;
    latest_id[0] = 0; pending_until = override_until = 0; playback_override = false;
    volume_override = false; volume_until = 0;
    unlock();
}
static void expire(int64_t now)
{
    if (state.pending && now >= pending_until) {
        state.pending = false; state.result_unknown = true;
        head = count = 0; // Never deliver an expired click much later.
    }
    if (now - state.updated_us >= ACTION_TIMEOUT_US) state.online = false;
}
bool luna_music_get_state(luna_music_state_t *out)
{
    if (!lock()) return false;
    int64_t now = esp_timer_get_time(); expire(now);
    *out = state;
    if (playback_override && now < override_until) out->playing = desired_playing;
    if (volume_override && now < volume_until) out->volume = desired_volume;
    unlock(); return true;
}
bool luna_music_enqueue(const char *name, int value)
{
    bool playback = !strcmp(name, "music.play") || !strcmp(name, "music.pause");
    bool volume = !strcmp(name, "music.volume_set") || !strcmp(name, "music.mute");
    if (!playback && !volume && strcmp(name, "music.previous") && strcmp(name, "music.next")) return false;
    if (!strcmp(name, "music.volume_set") && (value < 0 || value > 100)) return false;
    if (!lock()) return false;
    int64_t now = esp_timer_get_time(); expire(now);
    if (!state.online || !boot[0] || count == ACTION_CAPACITY || serial == UINT32_MAX ||
        (volume ? !state.volume_available : !state.available || !state.controllable)) { unlock(); return false; }
    action_t *a = &actions[(head + count) % ACTION_CAPACITY];
    snprintf(a->id, sizeof(a->id), "%s-%08" PRIx32, boot, ++serial);
    strlcpy(a->action, name, sizeof(a->action)); a->value = value; a->created_us = now;
    strlcpy(latest_id, a->id, sizeof(latest_id)); count++;
    state.pending = true; state.result_unknown = false; pending_until = now + ACTION_TIMEOUT_US;
    if (playback) {
        desired_playing = !strcmp(name, "music.play"); playback_override = true;
        override_until = pending_until;
    }
    if (!strcmp(name, "music.volume_set")) {
        state.volume = desired_volume = value; volume_override = true; volume_until = pending_until;
    }
    unlock(); return true;
}

int luna_music_message(const char *type, const cJSON *root, const char *session,
                       char *reply, size_t capacity)
{
    int result = BLE_ATT_ERR_UNLIKELY;
    if (!strcmp(type, "state_snapshot")) {
        const cJSON *music = cJSON_GetObjectItemCaseSensitive(root, "music");
        const cJSON *volume = cJSON_GetObjectItemCaseSensitive(root, "volume");
        luna_music_state_t incoming = {0};
        const cJSON *percent = cJSON_GetObjectItemCaseSensitive(volume, "percent");
        if (!cJSON_IsObject(music) || !cJSON_IsObject(volume) ||
            !boolean(music, "available", &incoming.available) ||
            !boolean(music, "controllable", &incoming.controllable) ||
            !boolean(music, "playing", &incoming.playing) ||
            !text(music, "title", incoming.title, sizeof(incoming.title)) ||
            !text(music, "artist", incoming.artist, sizeof(incoming.artist)) ||
            !boolean(volume, "available", &incoming.volume_available) ||
            !boolean(volume, "muted", &incoming.muted) || !cJSON_IsNumber(percent) ||
            !isfinite(percent->valuedouble) || floor(percent->valuedouble) != percent->valuedouble ||
            percent->valuedouble < 0 || percent->valuedouble > 100) return result;
        incoming.volume = (unsigned)percent->valuedouble;
        incoming.online = true; incoming.updated_us = esp_timer_get_time();
        if (!lock()) return BLE_ATT_ERR_INSUFFICIENT_RES;
        expire(incoming.updated_us);
        incoming.pending = state.pending; incoming.result_unknown = state.result_unknown;
        incoming.last_ok = state.last_ok; incoming.has_result = state.has_result;
        state = incoming;
        if (!state.pending && playback_override && state.playing == desired_playing) playback_override = false;
        if (!state.pending && volume_override && state.volume == desired_volume) volume_override = false;
        char action_json[160] = "null";
        while (count && incoming.updated_us - actions[head].created_us >= ACTION_TIMEOUT_US) {
            head = (head + 1) % ACTION_CAPACITY; count--;
        }
        if (count) {
            const action_t *a = &actions[head]; char value[12] = "null";
            if (!strcmp(a->action, "music.volume_set")) snprintf(value, sizeof(value), "%d", a->value);
            snprintf(action_json, sizeof(action_json), "{\"request_id\":\"%s\",\"action\":\"%s\",\"value\":%s}", a->id, a->action, value);
            head = (head + 1) % ACTION_CAPACITY; count--;
            // Once offered it is removed, even if notify/ack is lost. Never replay.
        }
        snprintf(reply, capacity, "{\"v\":1,\"type\":\"state_snapshot_result\",\"session\":\"%s\",\"boot_id\":\"%s\",\"accepted\":true,\"action_request\":%s}", session, boot, action_json);
        result = 0; unlock();
    } else if (!strcmp(type, "action_result")) {
        char id[26]; bool ok, known;
        if (!text(root, "request_id", id, sizeof(id)) || !boolean(root, "ok", &ok) ||
            !boolean(root, "result_known", &known)) return result;
        if (strlen(id) != 25 || id[16] != '-' || strncmp(id, boot, 16)) return result;
        for (unsigned i = 17; i < 25; i++) if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return result;
        if (!lock()) return BLE_ATT_ERR_INSUFFICIENT_RES;
        if (!strcmp(id, latest_id)) {
            state.pending = false; state.last_ok = ok; state.result_unknown = !known; state.has_result = true;
            // GSMTC can lag a successful explicit play/pause; reconcile up to 3 s.
            override_until = known && ok ? esp_timer_get_time() + 3000000 : 0;
            volume_until = known && ok ? esp_timer_get_time() + 3000000 : 0;
        }
        snprintf(reply, capacity, "{\"v\":1,\"type\":\"action_result_ack\",\"session\":\"%s\",\"boot_id\":\"%s\",\"accepted\":true}", session, boot);
        result = 0; unlock();
    }
    return result;
}
