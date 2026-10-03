// Optional B0 coexistence test. No PC HTTP channel or C6 firmware writes.
#include <stdatomic.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "luna_ble_wifi.h"
#include "luna_weather.h"
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
#include "luna_time.h"
#endif

static const char *TAG = "luna_ble_wifi";
static atomic_bool online, configured, available, cached;
static unsigned retries;
static esp_timer_handle_t recovery_timer;

static void recover(void *arg)
{
    (void)arg;
    if (!atomic_load(&online)) {
        if (esp_wifi_connect() != ESP_OK) esp_timer_start_once(recovery_timer, 30000000);
    }
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&online, false);
        luna_weather_set_network_ready(false);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        luna_time_set_network_ready(false);
#endif
        if (++retries <= 3) esp_wifi_connect();
        else if (!esp_timer_is_active(recovery_timer)) esp_timer_start_once(recovery_timer, 30000000);
        ESP_LOGW(TAG, "Wi-Fi offline; BLE remains independent");
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&online, true); retries = 0;
        if (esp_timer_is_active(recovery_timer)) esp_timer_stop(recovery_timer);
        luna_weather_set_network_ready(true);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        luna_time_set_network_ready(true);
#endif
        ESP_LOGI(TAG, "Wi-Fi online; device-owned HTTPS weather enabled");
    }
}

static void weather_update(const luna_weather_state_t *state, void *context)
{
    (void)context;
    atomic_store(&configured, state->configured && state->coordinates_valid);
    atomic_store(&available, state->available);
    atomic_store(&cached, state->stale);
    ESP_LOGI(TAG, "Weather configured=%u available=%u cached=%u",
             (unsigned)atomic_load(&configured), (unsigned)state->available, (unsigned)state->stale);
}

luna_ble_wifi_status_t luna_ble_wifi_status(void)
{
    return (luna_ble_wifi_status_t){atomic_load(&online), atomic_load(&configured),
                                 atomic_load(&available), atomic_load(&cached)};
}

esp_err_t luna_ble_wifi_start(void)
{
    if (!CONFIG_LUNA_WIFI_SSID[0]) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    esp_err_t rc = esp_event_loop_create_default();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) return rc;
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta(), ESP_ERR_NO_MEM, TAG, "station interface failed");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "existing C6 Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "RAM-only Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL), TAG, "Wi-Fi events failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL), TAG, "IP events failed");
    const esp_timer_create_args_t timer = {.callback=recover, .name="ble_wifi_recover"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &recovery_timer), TAG, "recovery timer failed");
    ESP_RETURN_ON_ERROR(luna_weather_start(weather_update, NULL), TAG, "weather worker failed");
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CONFIG_LUNA_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CONFIG_LUNA_WIFI_PASSWORD, sizeof(config.sta.password));
    config.sta.threshold.authmode = CONFIG_LUNA_WIFI_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "station config failed");
    return esp_wifi_start();
}
