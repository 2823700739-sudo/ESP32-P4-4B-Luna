#include "luna_agent_client.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "luna_usb.h"

#define RESPONSE_CAPACITY (LUNA_USB_MAX_PAYLOAD + 1)
#define COVER_CAPACITY (256 * 1024)
#define URL_CAPACITY 160

typedef struct {
    char data[RESPONSE_CAPACITY];
    size_t length;
    bool overflow;
} response_buffer_t;

typedef struct {
    uint8_t *data;
    size_t length;
    bool overflow;
} cover_buffer_t;

static const char *TAG = "luna_agent";
static SemaphoreHandle_t s_http_mutex;

esp_err_t luna_agent_client_init(void)
{
    if (s_http_mutex != NULL) {
        return ESP_OK;
    }
    s_http_mutex = xSemaphoreCreateMutex();
    return s_http_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t http_lock(void)
{
    ESP_RETURN_ON_FALSE(s_http_mutex != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Agent client is not initialized");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(6000)) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "Timed out waiting for the HTTP client");
    return ESP_OK;
}

static void http_unlock(void)
{
    xSemaphoreGive(s_http_mutex);
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    response_buffer_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t remaining = sizeof(response->data) - response->length - 1;
    const size_t copy_length = (size_t)event->data_len < remaining ? (size_t)event->data_len : remaining;
    if (copy_length > 0) {
        memcpy(response->data + response->length, event->data, copy_length);
        response->length += copy_length;
        response->data[response->length] = '\0';
    }
    if (copy_length != (size_t)event->data_len) {
        response->overflow = true;
    }
    return ESP_OK;
}

static esp_err_t cover_event_handler(esp_http_client_event_t *event)
{
    cover_buffer_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t incoming = (size_t)event->data_len;
    if (incoming > COVER_CAPACITY - response->length) {
        response->overflow = true;
        return ESP_OK;
    }
    uint8_t *resized = realloc(response->data, response->length + incoming);
    if (resized == NULL) {
        response->overflow = true;
        return ESP_OK;
    }
    response->data = resized;
    memcpy(response->data + response->length, event->data, incoming);
    response->length += incoming;
    return ESP_OK;
}

static bool build_url(char *url, size_t capacity, const char *path)
{
    const int written = snprintf(url, capacity, "http://%s:%d%s", CONFIG_LUNA_AGENT_HOST,
                                 CONFIG_LUNA_AGENT_PORT, path);
    return written > 0 && (size_t)written < capacity;
}

static void copy_json_string(const cJSON *object, const char *name, char *destination,
                             size_t capacity, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    const char *source = cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : fallback;
    source = source != NULL ? source : "";
    const size_t source_length = strlcpy(destination, source, capacity);
    if (capacity < 2 || source_length < capacity) {
        return;
    }

    /* strlcpy truncates by byte count. Remove a partial UTF-8 code point at the end. */
    if ((((const unsigned char *)source)[capacity - 1] & 0xC0U) == 0x80U) {
        size_t end = capacity - 1;
        while (end > 0 && (((unsigned char)destination[end - 1] & 0xC0U) == 0x80U)) {
            --end;
        }
        if (end > 0) {
            --end;
        }
        destination[end] = '\0';
    }
}

static bool json_bool(const cJSON *object, const char *name, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static int json_int(const cJSON *object, const char *name, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool parse_music(const cJSON *object, luna_music_state_t *music)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    music->controllable = json_bool(object, "controllable", false);
    music->metadata_available = json_bool(object, "metadata_available", false);
    music->playing = json_bool(object, "playing", false);
    music->cover_available = json_bool(object, "cover_available", false);
    copy_json_string(object, "title", music->title, sizeof(music->title), "No active media");
    copy_json_string(object, "artist", music->artist, sizeof(music->artist), "");
    copy_json_string(object, "cover_id", music->cover_id, sizeof(music->cover_id), "");
    copy_json_string(object, "cover_content_type", music->cover_content_type,
                     sizeof(music->cover_content_type), "");
    return true;
}

static bool parse_volume(const cJSON *object, luna_volume_state_t *volume)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    volume->available = json_bool(object, "available", false);
    volume->muted = json_bool(object, "muted", false);
    const int percent = json_int(object, "percent", 0);
    volume->percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    return true;
}

static esp_err_t parse_state_response(const char *json, luna_agent_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->codex.remaining_percent = -1;
    state->codex.weekly_remaining_percent = -1;
    state->weather.temperature_c = -999;

    cJSON *root = cJSON_Parse(json);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_ERR_INVALID_RESPONSE, TAG, "Agent JSON is invalid");

    const int protocol = json_int(root, "protocol_version", 0);
    if (protocol != 1) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Unsupported protocol version %d", protocol);
        return ESP_ERR_INVALID_VERSION;
    }

    state->sequence = (uint32_t)json_int(root, "sequence", 0);
    copy_json_string(root, "generated_at", state->generated_at, sizeof(state->generated_at), "");

    const cJSON *pc = cJSON_GetObjectItemCaseSensitive(root, "pc");
    if (cJSON_IsObject(pc)) {
        state->pc.online = json_bool(pc, "online", false);
        copy_json_string(pc, "name", state->pc.name, sizeof(state->pc.name), "Windows PC");
    }

    parse_music(cJSON_GetObjectItemCaseSensitive(root, "music"), &state->music);
    parse_volume(cJSON_GetObjectItemCaseSensitive(root, "volume"), &state->volume);

    const cJSON *codex = cJSON_GetObjectItemCaseSensitive(root, "codex");
    if (cJSON_IsObject(codex)) {
        state->codex.available = json_bool(codex, "available", false);
        state->codex.remaining_percent = json_int(codex, "remaining_percent", -1);
        state->codex.weekly_remaining_percent =
            json_int(codex, "weekly_remaining_percent", -1);
        copy_json_string(codex, "reset_at", state->codex.reset_at,
                         sizeof(state->codex.reset_at), "");
        copy_json_string(codex, "weekly_reset_at", state->codex.weekly_reset_at,
                         sizeof(state->codex.weekly_reset_at), "");
        copy_json_string(codex, "summary", state->codex.summary,
                         sizeof(state->codex.summary), "");
        copy_json_string(codex, "reset_text", state->codex.reset_text,
                         sizeof(state->codex.reset_text), "");
    }

    const cJSON *projects = cJSON_GetObjectItemCaseSensitive(root, "recent_projects");
    const cJSON *project = cJSON_IsArray(projects) ? cJSON_GetArrayItem(projects, 0) : NULL;
    if (cJSON_IsObject(project)) {
        copy_json_string(project, "name", state->project.name, sizeof(state->project.name),
                         "No recent project");
        copy_json_string(project, "path", state->project.path, sizeof(state->project.path), "");
    } else {
        strlcpy(state->project.name, "No recent project", sizeof(state->project.name));
    }

    const cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    if (cJSON_IsObject(weather)) {
        state->weather.configured = json_bool(weather, "configured", false);
        state->weather.available = json_bool(weather, "available", false);
        state->weather.stale = json_bool(weather, "stale", false);
        state->weather.temperature_c = json_int(weather, "temperature_c", -999);
        copy_json_string(weather, "location", state->weather.location,
                         sizeof(state->weather.location), "Not configured");
        copy_json_string(weather, "condition", state->weather.condition,
                         sizeof(state->weather.condition), "Waiting for weather adapter");
        copy_json_string(weather, "observed_at", state->weather.observed_at,
                         sizeof(state->weather.observed_at), "");
        copy_json_string(weather, "summary", state->weather.summary,
                         sizeof(state->weather.summary), "Location not configured");
        copy_json_string(weather, "details", state->weather.details,
                         sizeof(state->weather.details), "");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t parse_action_response(const char *json,
                                       luna_agent_action_result_t *action_result)
{
    cJSON *root = cJSON_Parse(json);
    ESP_RETURN_ON_FALSE(root != NULL, ESP_ERR_INVALID_RESPONSE, TAG,
                        "Agent action JSON is invalid");
    if (!json_bool(root, "ok", false)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (action_result != NULL) {
        memset(action_result, 0, sizeof(*action_result));
        action_result->has_music = parse_music(
            cJSON_GetObjectItemCaseSensitive(root, "music"), &action_result->music);
        action_result->has_volume = parse_volume(
            cJSON_GetObjectItemCaseSensitive(root, "volume"), &action_result->volume);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

bool luna_agent_is_configured(void)
{
    return CONFIG_LUNA_AGENT_HOST[0] != '\0' && CONFIG_LUNA_AGENT_TOKEN[0] != '\0';
}

esp_err_t luna_agent_fetch_state(luna_agent_state_t *state)
{
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "State pointer is null");

    if (luna_usb_is_ready()) {
        response_buffer_t *usb_response = calloc(1, sizeof(*usb_response));
        if (usb_response == NULL) {
            return ESP_ERR_NO_MEM;
        }
        size_t usb_length = 0;
        esp_err_t usb_result = luna_usb_exchange(
            LUNA_LINK_MESSAGE_STATE_REQUEST, LUNA_LINK_MESSAGE_STATE_SNAPSHOT,
            NULL, 0, usb_response->data, sizeof(usb_response->data) - 1,
            &usb_length, 2500);
        if (usb_result == ESP_OK) {
            usb_response->data[usb_length] = '\0';
            usb_result = parse_state_response(usb_response->data, state);
        }
        free(usb_response);
        if (usb_result == ESP_OK) {
            ESP_LOGD(TAG, "Agent state received over USB");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "USB state request failed, trying HTTP: %s",
                 esp_err_to_name(usb_result));
    }

    ESP_RETURN_ON_FALSE(luna_agent_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "PC agent host or token is not configured");

    char url[URL_CAPACITY];
    ESP_RETURN_ON_FALSE(build_url(url, sizeof(url), "/api/v1/state"), ESP_ERR_INVALID_SIZE, TAG,
                        "Agent URL is too long");
    ESP_RETURN_ON_ERROR(http_lock(), TAG, "Agent HTTP lock failed");

    response_buffer_t *response = calloc(1, sizeof(*response));
    if (response == NULL) {
        http_unlock();
        ESP_LOGE(TAG, "Agent response buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = 2500,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response);
        http_unlock();
        ESP_LOGE(TAG, "HTTP client allocation failed");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "X-Luna-Token", CONFIG_LUNA_AGENT_TOKEN);

    esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    http_unlock();
    if (result != ESP_OK) {
        free(response);
        ESP_LOGE(TAG, "Agent state request failed: %s", esp_err_to_name(result));
        return result;
    }
    if (response->overflow) {
        free(response);
        ESP_LOGE(TAG, "Agent response exceeded %d bytes", RESPONSE_CAPACITY);
        return ESP_ERR_INVALID_SIZE;
    }
    if (status != 200) {
        free(response);
        ESP_LOGE(TAG, "Agent returned HTTP %d", status);
        return ESP_ERR_HTTP_BASE + status;
    }

    result = parse_state_response(response->data, state);
    free(response);
    return result;
}

esp_err_t luna_agent_fetch_cover(uint8_t **data, size_t *length)
{
    ESP_RETURN_ON_FALSE(data != NULL && length != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Cover output pointer is null");
    ESP_RETURN_ON_FALSE(luna_agent_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "PC agent is not configured");
    *data = NULL;
    *length = 0;

    char url[URL_CAPACITY];
    ESP_RETURN_ON_FALSE(build_url(url, sizeof(url), "/api/v1/music/cover"),
                        ESP_ERR_INVALID_SIZE, TAG, "Agent URL is too long");
    ESP_RETURN_ON_ERROR(http_lock(), TAG, "Cover HTTP lock failed");

    cover_buffer_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = cover_event_handler,
        .user_data = &response,
        .timeout_ms = 3500,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        http_unlock();
        ESP_LOGE(TAG, "HTTP client allocation failed");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "X-Luna-Token", CONFIG_LUNA_AGENT_TOKEN);

    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    http_unlock();
    if (result != ESP_OK) {
        free(response.data);
        return result;
    }
    if (response.overflow) {
        free(response.data);
        return ESP_ERR_INVALID_SIZE;
    }
    if (status != 200 || response.length == 0) {
        free(response.data);
        return status == 404 ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_RESPONSE;
    }

    *data = response.data;
    *length = response.length;
    return ESP_OK;
}

esp_err_t luna_agent_send_action(const char *action, uint64_t request_id, int value,
                                 luna_agent_action_result_t *action_result)
{
    ESP_RETURN_ON_FALSE(action != NULL && action[0] != '\0', ESP_ERR_INVALID_ARG, TAG,
                        "Action is empty");

    char body[192];
    const int body_length = value >= 0
        ? snprintf(body, sizeof(body),
                   "{\"request_id\":\"p4-%" PRIu64 "\",\"action\":\"%s\",\"value\":%d}",
                   request_id, action, value)
        : snprintf(body, sizeof(body),
                   "{\"request_id\":\"p4-%" PRIu64 "\",\"action\":\"%s\"}",
                   request_id, action);
    ESP_RETURN_ON_FALSE(body_length > 0 && (size_t)body_length < sizeof(body),
                        ESP_ERR_INVALID_SIZE, TAG, "Action body is too long");

    if (luna_usb_is_ready()) {
        response_buffer_t *usb_response = calloc(1, sizeof(*usb_response));
        if (usb_response == NULL) {
            return ESP_ERR_NO_MEM;
        }
        size_t usb_length = 0;
        esp_err_t usb_result = luna_usb_exchange(
            LUNA_LINK_MESSAGE_ACTION_REQUEST, LUNA_LINK_MESSAGE_ACTION_RESULT,
            body, (size_t)body_length, usb_response->data,
            sizeof(usb_response->data) - 1, &usb_length, 3000);
        if (usb_result == ESP_OK) {
            usb_response->data[usb_length] = '\0';
            usb_result = parse_action_response(usb_response->data, action_result);
        }
        free(usb_response);
        if (usb_result == ESP_OK) {
            ESP_LOGI(TAG, "Agent action delivered over USB: %s", action);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "USB action failed, trying HTTP: %s", esp_err_to_name(usb_result));
    }

    ESP_RETURN_ON_FALSE(luna_agent_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "PC agent is not configured");

    char url[URL_CAPACITY];
    ESP_RETURN_ON_FALSE(build_url(url, sizeof(url), "/api/v1/actions"), ESP_ERR_INVALID_SIZE, TAG,
                        "Agent URL is too long");
    ESP_RETURN_ON_ERROR(http_lock(), TAG, "Action HTTP lock failed");

    response_buffer_t *response = calloc(1, sizeof(*response));
    if (response == NULL) {
        http_unlock();
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = 2500,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response);
        http_unlock();
        ESP_LOGE(TAG, "HTTP client allocation failed");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "X-Luna-Token", CONFIG_LUNA_AGENT_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, body_length);

    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    http_unlock();
    const bool overflow = response->overflow;
    if (result != ESP_OK || overflow || status < 200 || status >= 300) {
        free(response);
        ESP_RETURN_ON_ERROR(result, TAG, "Agent action request failed");
        ESP_RETURN_ON_FALSE(!overflow, ESP_ERR_INVALID_SIZE, TAG,
                            "Agent action response overflow");
        ESP_LOGW(TAG, "Agent action returned HTTP %d", status);
        return ESP_ERR_HTTP_BASE + status;
    }

    esp_err_t parse_result = parse_action_response(response->data, action_result);
    free(response);
    return parse_result;
}
