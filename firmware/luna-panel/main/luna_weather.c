#include "luna_weather.h"
#include "luna_weather_parse.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define WEATHER_RESPONSE_CAPACITY 8192U
#define WEATHER_URL_CAPACITY 512U
#define WEATHER_REFRESH_SECONDS (20 * 60)
#define WEATHER_RETRY_SECONDS 60
#define WEATHER_CLOCK_RETRY_SECONDS 10
#define WEATHER_CACHE_VERSION 2U
#define WEATHER_NVS_NAMESPACE "luna_weather"
#define WEATHER_NVS_KEY "snapshot"

typedef struct {
    char data[WEATHER_RESPONSE_CAPACITY];
    size_t length;
    bool overflow;
} weather_response_t;

typedef struct {
    uint32_t version;
    luna_weather_state_t state;
    int64_t last_trusted_utc;
} weather_cache_t;

static const char *TAG = "luna_weather";
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static luna_weather_state_t s_weather;
static luna_weather_metrics_t s_metrics;
static luna_weather_update_cb_t s_callback;
static void *s_callback_context;
static bool s_network_ready;
static bool s_refresh_requested;
static int64_t s_last_trusted_utc;

static bool location_is_valid(const luna_weather_state_t *weather)
{
    return weather != NULL && weather->configured && weather->coordinates_valid &&
           weather->location[0] != '\0' &&
           memchr(weather->location, '\0', sizeof(weather->location)) != NULL &&
           isfinite(weather->latitude) && weather->latitude >= -90.0 &&
           weather->latitude <= 90.0 && isfinite(weather->longitude) &&
           weather->longitude >= -180.0 && weather->longitude <= 180.0;
}

static bool same_location(const luna_weather_state_t *left,
                          const luna_weather_state_t *right)
{
    return location_is_valid(left) && location_is_valid(right) &&
           left->latitude == right->latitude && left->longitude == right->longitude &&
           strcmp(left->location, right->location) == 0;
}

static void publish(const luna_weather_state_t *weather)
{
    if (s_callback != NULL) {
        s_callback(weather, s_callback_context);
    }
}

static void save_cache_locked(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WEATHER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(result));
        return;
    }
    const weather_cache_t cache = {
        .version = WEATHER_CACHE_VERSION,
        .state = s_weather,
        .last_trusted_utc = s_last_trusted_utc,
    };
    result = nvs_set_blob(handle, WEATHER_NVS_KEY, &cache, sizeof(cache));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "NVS weather save failed: %s", esp_err_to_name(result));
    }
}

static void load_cache(void)
{
    memset(&s_weather, 0, sizeof(s_weather));
    s_weather.temperature_c = -999;
    strlcpy(s_weather.summary, "Weather location not configured", sizeof(s_weather.summary));
    strlcpy(s_weather.details, "Set location on Windows once", sizeof(s_weather.details));

    nvs_handle_t handle;
    if (nvs_open(WEATHER_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    weather_cache_t cache = {0};
    size_t length = sizeof(cache);
    const esp_err_t result = nvs_get_blob(handle, WEATHER_NVS_KEY, &cache, &length);
    nvs_close(handle);
    const bool current_cache = length == sizeof(cache) &&
                               cache.version == WEATHER_CACHE_VERSION;
    const bool legacy_cache = length == offsetof(weather_cache_t, last_trusted_utc) &&
                              cache.version == 1;
    if (result != ESP_OK || (!current_cache && !legacy_cache) ||
        !location_is_valid(&cache.state)) {
        return;
    }
    s_weather = cache.state;
    s_weather.stale = s_weather.available;
    if (current_cache && cache.last_trusted_utc >= 1700000000 &&
        cache.last_trusted_utc <= 4102444800LL) {
        s_last_trusted_utc = cache.last_trusted_utc;
        // The new BLE profile must reacquire time after a reboot. A saved
        // weather timestamp is cache metadata, not a battery-backed RTC.
    }
    ESP_LOGI(TAG, "Loaded device weather location and cache from NVS");
}

static esp_err_t weather_http_event(esp_http_client_event_t *event)
{
    weather_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t incoming = (size_t)event->data_len;
    const size_t remaining = sizeof(response->data) - response->length - 1;
    const size_t copied = incoming < remaining ? incoming : remaining;
    if (copied > 0) {
        memcpy(response->data + response->length, event->data, copied);
        response->length += copied;
        response->data[response->length] = '\0';
    }
    response->overflow |= copied != incoming;
    return ESP_OK;
}

static esp_err_t fetch_forecast(luna_weather_state_t *weather, luna_weather_metrics_t *metrics)
{
    char url[WEATHER_URL_CAPACITY];
    const int written = snprintf(
        url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
        "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&timezone=auto&forecast_days=1&temperature_unit=celsius&wind_speed_unit=ms",
        weather->latitude, weather->longitude);
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    weather_response_t *response = calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response);
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    esp_err_t parsed = result;
    if (parsed == ESP_OK) {
        parsed = status == 200 && !response->overflow && response->length > 0
                     ? luna_weather_parse_forecast(response->data, weather, metrics)
                     : ESP_ERR_INVALID_RESPONSE;
    }
    free(response);
    return parsed;
}

static void weather_task(void *arg)
{
    (void)arg;
    int64_t next_fetch_us = 0;
    while (true) {
        luna_weather_state_t settings;
        bool online;
        bool requested;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        settings = s_weather;
        online = s_network_ready;
        requested = s_refresh_requested;
        s_refresh_requested = false;
        xSemaphoreGive(s_mutex);

        if (!online || !location_is_valid(&settings)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        const int64_t now_us = esp_timer_get_time();
        if (!requested && now_us < next_fetch_us) {
            const int64_t remaining_ms = (next_fetch_us - now_us) / 1000;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((uint32_t)remaining_ms + 1));
            continue;
        }
        if (time(NULL) < 1700000000) {
            ESP_LOGW(TAG, "Waiting for valid time before HTTPS weather request");
            next_fetch_us = now_us + (int64_t)WEATHER_CLOCK_RETRY_SECONDS * 1000000;
            continue;
        }

        luna_weather_state_t fetched = settings;
        luna_weather_metrics_t metrics = {0};
        ESP_LOGI(TAG, "Device HTTPS weather request started");
        const esp_err_t result = fetch_forecast(&fetched, &metrics);
        luna_weather_state_t published;
        bool should_publish = false;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (s_network_ready && same_location(&settings, &s_weather)) {
            if (result == ESP_OK) {
                s_weather = fetched;
                s_metrics = metrics;
                s_last_trusted_utc = time(NULL);
                save_cache_locked();
                next_fetch_us = esp_timer_get_time() +
                                (int64_t)WEATHER_REFRESH_SECONDS * 1000000;
                ESP_LOGI(TAG, "Device weather refreshed from Open-Meteo");
            } else {
                s_weather.stale = s_weather.available;
                next_fetch_us = esp_timer_get_time() +
                                (int64_t)WEATHER_RETRY_SECONDS * 1000000;
                ESP_LOGW(TAG, "Device weather fetch failed: %s", esp_err_to_name(result));
            }
            published = s_weather;
            should_publish = true;
        }
        xSemaphoreGive(s_mutex);
        if (should_publish) {
            publish(&published);
        }
    }
}

esp_err_t luna_weather_start(luna_weather_update_cb_t callback, void *context)
{
    if (s_mutex != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_callback = callback;
    s_callback_context = context;
    load_cache();
    publish(&s_weather);
    if (xTaskCreate(weather_task, "luna_weather", 12288, NULL, 3, &s_task) != pdPASS) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void luna_weather_set_location(const luna_weather_state_t *pc_weather)
{
    if (s_mutex == NULL || !location_is_valid(pc_weather)) {
        return;
    }
    luna_weather_state_t published;
    bool changed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!same_location(&s_weather, pc_weather)) {
        memset(&s_weather, 0, sizeof(s_weather));
        memset(&s_metrics, 0, sizeof(s_metrics));
        s_weather.configured = true;
        s_weather.coordinates_valid = true;
        s_weather.latitude = pc_weather->latitude;
        s_weather.longitude = pc_weather->longitude;
        s_weather.temperature_c = -999;
        strlcpy(s_weather.location, pc_weather->location, sizeof(s_weather.location));
        strlcpy(s_weather.summary, "Waiting for Luna Wi-Fi weather",
                sizeof(s_weather.summary));
        strlcpy(s_weather.details, "Open-Meteo / Luna Wi-Fi",
                sizeof(s_weather.details));
        save_cache_locked();
        s_refresh_requested = true;
        published = s_weather;
        changed = true;
        ESP_LOGI(TAG, "Device weather location updated from PC settings");
    }
    xSemaphoreGive(s_mutex);
    if (changed) {
        publish(&published);
        xTaskNotifyGive(s_task);
    }
}

void luna_weather_set_network_ready(bool ready)
{
    if (s_mutex == NULL) {
        return;
    }
    luna_weather_state_t published;
    bool changed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_network_ready != ready) {
        s_network_ready = ready;
        s_refresh_requested = ready;
        if (!ready && s_weather.available) {
            s_weather.stale = true;
            published = s_weather;
            changed = true;
        }
    }
    xSemaphoreGive(s_mutex);
    if (changed) {
        publish(&published);
    }
    if (ready) {
        xTaskNotifyGive(s_task);
    }
}

bool luna_weather_snapshot(luna_weather_state_t *weather, luna_weather_metrics_t *metrics)
{
    if (!weather || !metrics || !s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    *weather = s_weather; *metrics = s_metrics;
    xSemaphoreGive(s_mutex); return true;
}
