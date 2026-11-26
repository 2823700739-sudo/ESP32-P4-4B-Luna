// Device-owned Wi-Fi/weather; short-lived phone AP and secured BLE share one worker.
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "luna_ble_wifi.h"
#include "luna_weather.h"
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
#include "luna_time.h"
#include "luna_setup_portal.h"
#include "esp_random.h"
#endif

static const char *TAG = "luna_ble_wifi";
static atomic_bool online, configured, available, cached, reconfiguring;
static atomic_uint retries;
static esp_timer_handle_t recovery_timer;
static QueueHandle_t setup_queue;
static SemaphoreHandle_t setup_mutex;
static luna_ble_wifi_setup_status_t setup_status;
static bool driver_initialized, wifi_events, ip_events;
static atomic_bool station_enabled;
typedef struct {
    bool trial;
    luna_weather_config_t config;
    luna_ble_wifi_setup_status_t prior;
} setup_job_t;
static esp_err_t apply_settings(const luna_weather_config_t *settings);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
static luna_setup_ap_state_t ap;
static atomic_int ap_request;
static int64_t ap_deadline, ap_close_after;
static bool trial_active, ap_committed;
static luna_setup_scan_t scan;
static atomic_bool scan_busy;
static atomic_int scan_done;
static bool scan_started;
static int64_t scan_deadline, scan_next;

bool luna_setup_scan_snapshot(luna_setup_scan_t *out)
{
    if (!out || !setup_mutex || xSemaphoreTake(setup_mutex,pdMS_TO_TICKS(5))!=pdTRUE) return false;
    *out=scan;
    xSemaphoreGive(setup_mutex);
    return true;
}

bool luna_setup_scan_request(void)
{
    if (!setup_mutex || xSemaphoreTake(setup_mutex,0)!=pdTRUE) return false;
    bool accepted=ap.active && setup_status.state!=1 && !atomic_load(&scan_busy) &&
                  esp_timer_get_time()>=scan_next;
    if (accepted) {
        memset(&scan,0,sizeof(scan));scan.state=1;
        atomic_store(&scan_busy,true);
        scan_next=esp_timer_get_time()+5000000;
    }
    xSemaphoreGive(setup_mutex);
    return accepted;
}

bool luna_setup_ap_snapshot(luna_setup_ap_state_t *out)
{
    if (!out || !setup_mutex || xSemaphoreTake(setup_mutex,pdMS_TO_TICKS(5))!=pdTRUE) return false;
    *out=ap;
    int64_t left=ap_deadline-esp_timer_get_time();
    out->remaining_seconds=ap.active && left>0?(unsigned)(left/1000000):0;
    xSemaphoreGive(setup_mutex);
    return true;
}
void luna_setup_ap_request(bool open) { atomic_store(&ap_request,open?1:-1); }
#endif

static void network_ready(bool ready)
{
    atomic_store(&online, ready);
    luna_weather_set_network_ready(ready);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    luna_time_set_network_ready(ready);
#endif
}

static void recover(void *arg)
{
    (void)arg;
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    if (atomic_load(&scan_busy)) {esp_timer_start_once(recovery_timer,5000000);return;}
#endif
    if (atomic_load(&station_enabled) && !atomic_load(&online) && !atomic_load(&reconfiguring)) {
        if (esp_wifi_connect() != ESP_OK) esp_timer_start_once(recovery_timer, 30000000);
    }
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    if (base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE && atomic_load(&scan_busy)) {
        const wifi_event_sta_scan_done_t *event=data;
        atomic_store(&scan_done,event && event->status==0?1:-1);
    }
#endif
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && atomic_load(&station_enabled)) esp_wifi_connect();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        network_ready(false);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        if (atomic_load(&scan_busy)) return; /* Worker resumes recovery after scan. */
#endif
        if (atomic_load(&reconfiguring) || !atomic_load(&station_enabled)) return;
        if (atomic_fetch_add(&retries, 1) < 3) esp_wifi_connect();
        else if (!esp_timer_is_active(recovery_timer)) esp_timer_start_once(recovery_timer, 30000000);
        ESP_LOGW(TAG, "Wi-Fi offline; BLE remains independent");
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && !atomic_load(&reconfiguring)) {
        atomic_store(&retries, 0);
        if (esp_timer_is_active(recovery_timer)) esp_timer_stop(recovery_timer);
        network_ready(true);
        ESP_LOGI(TAG, "Wi-Fi online; device-owned HTTPS weather enabled");
    }
}

static void weather_update(const luna_weather_state_t *state, void *context)
{
    (void)context;
    atomic_store(&configured, state->configured && state->coordinates_valid);
    atomic_store(&available, state->available);
    atomic_store(&cached, state->stale);
}

luna_ble_wifi_status_t luna_ble_wifi_status(void)
{
    return (luna_ble_wifi_status_t){atomic_load(&online), atomic_load(&configured),
                                 atomic_load(&available), atomic_load(&cached)};
}

bool luna_ble_wifi_setup_status(luna_ble_wifi_setup_status_t *out)
{
    if (!out || !setup_mutex || xSemaphoreTake(setup_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    *out=setup_status;
    xSemaphoreGive(setup_mutex);
    return true;
}

static bool queue_settings(const luna_weather_config_t *config, bool trial)
{
    if (!luna_weather_config_valid(config) || !setup_queue || !setup_mutex ||
        xSemaphoreTake(setup_mutex, 0) != pdTRUE) return false;
    bool accepted=false;
    bool busy=setup_status.state==1;
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    busy=busy || atomic_load(&scan_busy);
#endif
    if (!busy) {
        luna_ble_wifi_setup_status_t previous=setup_status;
        memcpy(setup_status.setup_id,config->setup_id,sizeof(setup_status.setup_id));
        setup_status.state=1; setup_status.error=0; setup_status.saved=false;
        setup_job_t job={.trial=trial,.config=*config,.prior=previous};
        accepted=xQueueSend(setup_queue,&job,0)==pdTRUE;
        luna_weather_config_clear(&job,sizeof(job));
        if (!accepted) setup_status=previous;
    }
    xSemaphoreGive(setup_mutex);
    return accepted;
}

bool luna_ble_wifi_configure(const luna_weather_config_t *config) { return queue_settings(config,false); }
bool luna_ble_wifi_try_network(const luna_weather_config_t *config) { return queue_settings(config,true); }

static void set_result(unsigned state, esp_err_t error, bool saved)
{
    xSemaphoreTake(setup_mutex, portMAX_DELAY);
    setup_status.state=state; setup_status.error=error; setup_status.saved=saved;
    if (saved) setup_status.wifi_configured=true;
    xSemaphoreGive(setup_mutex);
}

static bool load_settings(luna_weather_config_t *config)
{
    nvs_handle_t handle;
    if (nvs_open("luna_setup",NVS_READONLY,&handle)!=ESP_OK) return false;
    size_t length=sizeof(*config);
    esp_err_t rc=nvs_get_blob(handle,"weather_v1",config,&length);
    nvs_close(handle);
    return rc==ESP_OK && length==sizeof(*config) && luna_weather_config_valid(config);
}

static esp_err_t save_settings(const luna_weather_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t rc=nvs_open("luna_setup",NVS_READWRITE,&handle);
    if (rc!=ESP_OK) return rc;
    rc=nvs_set_blob(handle,"weather_v1",config,sizeof(*config));
    if (rc==ESP_OK) rc=nvs_commit(handle);
    nvs_close(handle);
    return rc;
}

/* All Hosted/C6 RPC and flash writes run on this worker, never the NimBLE host. */
static esp_err_t init_wifi(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(),TAG,"netif init failed");
    esp_err_t rc=esp_event_loop_create_default();
    if (rc!=ESP_OK && rc!=ESP_ERR_INVALID_STATE) return rc;
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") && !esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    if (!driver_initialized) {
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init),TAG,"existing C6 Wi-Fi init failed");
        driver_initialized=true;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM),TAG,"RAM-only Wi-Fi config failed");
    if (!wifi_events) {
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,network_event,NULL),TAG,"Wi-Fi events failed");
        wifi_events=true;
    }
    if (!ip_events) {
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,network_event,NULL),TAG,"IP events failed");
        ip_events=true;
    }
    return ESP_OK;
}

static esp_err_t apply_wifi(const luna_weather_config_t *settings)
{
    ESP_RETURN_ON_ERROR(init_wifi(),TAG,"Wi-Fi init failed");
    esp_err_t rc;
    atomic_store(&station_enabled,true);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    if (ap.active) {
        /* Keep the phone AP alive while trying/updating the upstream station. */
        (void)esp_wifi_disconnect();
    } else
#endif
    {
        (void)esp_wifi_stop();
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA),TAG,"station mode failed");
    }
    wifi_config_t config={0};
    memcpy(config.sta.ssid,settings->ssid,strlen(settings->ssid)); /* 32-byte SSIDs are valid */
    memcpy(config.sta.password,settings->password,strlen(settings->password));
    config.sta.threshold.authmode=settings->password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    rc=esp_wifi_set_config(WIFI_IF_STA,&config);
    luna_weather_config_clear(&config,sizeof(config));
    if (rc!=ESP_OK) return rc;
    atomic_store(&reconfiguring,false);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    if (ap.active) return ESP_OK;
#endif
    return esp_wifi_start();
}

#ifdef CONFIG_LUNA_BLE_B1_MUSIC
static void finish_scan(esp_err_t error)
{
    xSemaphoreTake(setup_mutex,portMAX_DELAY);
    scan.state=error==ESP_OK?2:3;scan.error=error;
    if (error!=ESP_OK) scan.count=0;
    atomic_store(&scan_busy,false);scan_started=false;
    xSemaphoreGive(setup_mutex);
    if (atomic_load(&station_enabled) && !atomic_load(&online) && !esp_timer_is_active(recovery_timer))
        esp_timer_start_once(recovery_timer,1000000);
}

static void service_scan(void)
{
    if (!atomic_load(&scan_busy)) return;
    if (!scan_started) {
        atomic_store(&scan_done,0);
        wifi_scan_config_t config={.show_hidden=false,.scan_type=WIFI_SCAN_TYPE_ACTIVE,
            .scan_time.active={.min=60,.max=120}};
        scan_started=true;scan_deadline=esp_timer_get_time()+12000000;
        esp_err_t rc=esp_wifi_scan_start(&config,false);
        if (rc!=ESP_OK) { (void)esp_wifi_clear_ap_list();finish_scan(rc); }
        return;
    }
    int done=atomic_load(&scan_done);
    if (!done && esp_timer_get_time()<scan_deadline) return;
    if (done!=1) {
        (void)esp_wifi_scan_stop();(void)esp_wifi_clear_ap_list();
        finish_scan(done?ESP_FAIL:ESP_ERR_TIMEOUT);return;
    }
    /* SDK returns strongest records first and releases its scan list on retrieval.
       Bounded heap keeps the Hosted result off the 6 KB provisioning task stack. */
    uint16_t count=32;
    wifi_ap_record_t *records=calloc(count,sizeof(*records));
    esp_err_t rc=records?esp_wifi_scan_get_ap_records(&count,records):ESP_ERR_NO_MEM;
    if (rc==ESP_OK && count>32) rc=ESP_FAIL;
    if (rc!=ESP_OK) (void)esp_wifi_clear_ap_list();
    xSemaphoreTake(setup_mutex,portMAX_DELAY);
    for (unsigned i=0;rc==ESP_OK && i<count && scan.count<LUNA_SETUP_NETWORK_LIMIT;i++) {
        /* Reuse strict UTF-8/control-byte validation; no credentials or BSSID leave the worker. */
        luna_weather_config_t valid={.version=1,.setup_id="00000000000000000000000000000000",
            .location="Scan"};
        memcpy(valid.ssid,records[i].ssid,32);
        if (!luna_weather_config_valid(&valid)) continue;
        bool secured=records[i].authmode!=WIFI_AUTH_OPEN,duplicate=false;
        for (unsigned j=0;j<scan.count;j++)
            if (!strcmp(scan.networks[j].ssid,valid.ssid) && scan.networks[j].secured==secured) duplicate=true;
        if (duplicate) continue;
        luna_setup_network_t *out=&scan.networks[scan.count++];
        memcpy(out->ssid,valid.ssid,sizeof(out->ssid));out->rssi=records[i].rssi;out->secured=secured;
    }
    xSemaphoreGive(setup_mutex);
    free(records);finish_scan(rc);
}

static esp_err_t open_ap(void)
{
    ESP_RETURN_ON_ERROR(init_wifi(),TAG,"setup Wi-Fi init failed");
    if (ap.active) return ESP_OK;
    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") && !esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    luna_setup_ap_state_t next={0};
    uint8_t bytes[3]; esp_fill_random(bytes,sizeof(bytes));
    snprintf(next.ssid,sizeof(next.ssid),"Luna-Setup-%02X%02X%02X",bytes[0],bytes[1],bytes[2]);
    luna_weather_config_clear(bytes,sizeof(bytes));
    wifi_config_t config={0};
    memcpy(config.ap.ssid,next.ssid,strlen(next.ssid));config.ap.ssid_len=strlen(next.ssid);
    config.ap.channel=1;config.ap.max_connection=1;config.ap.authmode=WIFI_AUTH_OPEN;
    /* APSTA preserves existing station operation; no routing/NAT to the home LAN. */
    esp_err_t rc=esp_wifi_set_mode(WIFI_MODE_APSTA);
    ESP_LOGI(TAG,"Phone setup AP mode rc=%d",rc);
    if (rc==ESP_OK) {
        rc=esp_wifi_set_config(WIFI_IF_AP,&config);
        ESP_LOGI(TAG,"Phone setup AP configuration rc=%d",rc);
    }
    luna_weather_config_clear(&config,sizeof(config));
    if (rc==ESP_OK) {
        rc=esp_wifi_start();ESP_LOGI(TAG,"Phone setup AP radio start rc=%d",rc);
    }
    if (rc==ESP_OK) {
        rc=luna_setup_portal_start();ESP_LOGI(TAG,"Phone setup HTTP start rc=%d",rc);
    }
    if (rc==ESP_OK) {
        xSemaphoreTake(setup_mutex,portMAX_DELAY);
        ap=next;ap.active=true;ap_deadline=esp_timer_get_time()+600000000LL;ap_close_after=0;ap_committed=false;
        xSemaphoreGive(setup_mutex);
        ESP_LOGI(TAG,"Phone setup AP opened for 10 minutes; open network, no password");
    } else {
        luna_setup_portal_stop();
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
    }
    luna_weather_config_clear(&next,sizeof(next));
    return rc;
}

static void close_ap(void)
{
    if (!ap.active) return;
    if (atomic_load(&scan_busy)) {
        if (scan_started) (void)esp_wifi_scan_stop();
        (void)esp_wifi_clear_ap_list();finish_scan(ESP_ERR_INVALID_STATE);
    }
    luna_setup_portal_stop();
    xSemaphoreTake(setup_mutex,portMAX_DELAY);
    luna_weather_config_clear(&ap,sizeof(ap));memset(&scan,0,sizeof(scan));scan_next=0;ap_deadline=ap_close_after=0;
    xSemaphoreGive(setup_mutex);
    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    if (trial_active) {
        /* Cancel/timeout restores the last committed network, never the trial. */
        luna_weather_config_t prior={0};
        if (load_settings(&prior)) (void)apply_settings(&prior);
        else if (CONFIG_LUNA_WIFI_SSID[0]) {
            strlcpy(prior.ssid,CONFIG_LUNA_WIFI_SSID,sizeof(prior.ssid));
            strlcpy(prior.password,CONFIG_LUNA_WIFI_PASSWORD,sizeof(prior.password));
            (void)apply_settings(&prior);
        } else {
            atomic_store(&station_enabled,false);(void)esp_wifi_disconnect();network_ready(false);
        }
        luna_weather_config_clear(&prior,sizeof(prior));trial_active=false;
    }
    ESP_LOGI(TAG,"Phone setup AP closed");
}

static void service_ap(void)
{
    int request=atomic_exchange(&ap_request,0);
    if (request>0 && !ap.active) {
        esp_err_t rc=open_ap();
        xSemaphoreTake(setup_mutex,portMAX_DELAY);ap.error=rc;xSemaphoreGive(setup_mutex);
    }
    if (request<0 || (ap.active && esp_timer_get_time()>=ap_deadline) ||
        (ap_close_after && esp_timer_get_time()>=ap_close_after)) close_ap();
}
#endif

static esp_err_t apply_settings(const luna_weather_config_t *settings)
{
    atomic_store(&reconfiguring,true);
    atomic_store(&retries,0);
    (void)esp_timer_stop(recovery_timer);
    network_ready(false);
    luna_weather_state_t location={.configured=true,.coordinates_valid=true,
                                  .latitude=settings->latitude,.longitude=settings->longitude};
    memcpy(location.location,settings->location,sizeof(location.location));
    if (location.location[0]) luna_weather_set_location(&location);
    esp_err_t rc=apply_wifi(settings);
    atomic_store(&reconfiguring,false);
    /* STA_START may already have run; an extra connect can return "connecting". */
    if (rc==ESP_OK) (void)esp_wifi_connect();
    return rc;
}

static void setup_worker(void *unused)
{
    (void)unused;
    luna_weather_config_t settings={0};
    if (load_settings(&settings)) {
        xSemaphoreTake(setup_mutex,portMAX_DELAY);
        memcpy(setup_status.setup_id,settings.setup_id,sizeof(setup_status.setup_id));
        setup_status.state=1;
        xSemaphoreGive(setup_mutex);
        esp_err_t rc=apply_settings(&settings);
        set_result(rc==ESP_OK?2:3,rc,true);
    } else {
        luna_weather_config_clear(&settings,sizeof(settings));
        esp_err_t rc=ESP_OK;
        if (CONFIG_LUNA_WIFI_SSID[0]) {
        /* Preserve legacy private builds when no runtime provisioning exists. */
        strlcpy(settings.ssid,CONFIG_LUNA_WIFI_SSID,sizeof(settings.ssid));
        strlcpy(settings.password,CONFIG_LUNA_WIFI_PASSWORD,sizeof(settings.password));
        xSemaphoreTake(setup_mutex,portMAX_DELAY);
        setup_status.wifi_configured=true;
        xSemaphoreGive(setup_mutex);
            rc=apply_settings(&settings); /* weather_start already restored legacy location */
        }
        set_result(rc==ESP_OK?0:3,rc,false);
    }
    luna_weather_config_clear(&settings,sizeof(settings));
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
    if (!setup_status.wifi_configured) luna_setup_ap_request(true);
#endif
    setup_job_t job={0};
    for (;;) {
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
        service_ap();
        service_scan();
        if (ap.active && ap_committed && !trial_active && atomic_load(&online) && !ap_close_after)
            ap_close_after=esp_timer_get_time()+20000000;
#endif
        if (xQueueReceive(setup_queue,&job,pdMS_TO_TICKS(250))!=pdTRUE) continue;
        settings=job.config;
        if (job.trial) {
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
            if (!ap.active) {
                xSemaphoreTake(setup_mutex,portMAX_DELAY);setup_status=job.prior;xSemaphoreGive(setup_mutex);
                luna_weather_config_clear(&job,sizeof(job));luna_weather_config_clear(&settings,sizeof(settings));
                continue;
            }
#endif
            atomic_store(&reconfiguring,true);network_ready(false);
            esp_err_t rc=apply_wifi(&settings);
            atomic_store(&reconfiguring,false);
            if (rc==ESP_OK) (void)esp_wifi_connect();
            xSemaphoreTake(setup_mutex,portMAX_DELAY);
            setup_status=job.prior;
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
            ap.error=rc;trial_active=true;ap_close_after=0;ap_committed=false;
#endif
            xSemaphoreGive(setup_mutex);
            luna_weather_config_clear(&job,sizeof(job));
            luna_weather_config_clear(&settings,sizeof(settings));
            continue;
        }
        xSemaphoreTake(setup_mutex,portMAX_DELAY);
        memcpy(setup_status.setup_id,settings.setup_id,sizeof(setup_status.setup_id));
        setup_status.state=1; setup_status.saved=false; setup_status.error=0;
        xSemaphoreGive(setup_mutex);
        esp_err_t rc=save_settings(&settings);
        if (rc==ESP_OK) {
            rc=apply_settings(&settings);
            set_result(rc==ESP_OK?2:3,rc,true);
#ifdef CONFIG_LUNA_BLE_B1_MUSIC
            trial_active=false;ap_committed=true;
#endif
        } else {
            /* Previous committed settings remain authoritative after a failed save. */
            set_result(4,rc,false);
        }
        luna_weather_config_clear(&settings,sizeof(settings));
        luna_weather_config_clear(&job,sizeof(job));
    }
}

esp_err_t luna_ble_wifi_start(void)
{
    if (setup_mutex) return ESP_ERR_INVALID_STATE;
    setup_mutex=xSemaphoreCreateMutex();
    setup_queue=xQueueCreate(1,sizeof(setup_job_t));
    if (!setup_mutex || !setup_queue) return ESP_ERR_NO_MEM;
    setup_status.state=1; /* Reject provisioning until boot settings finish loading. */
    const esp_timer_create_args_t timer={.callback=recover,.name="ble_wifi_recover"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer,&recovery_timer),TAG,"recovery timer failed");
    /* Load location/cache and expose setup even with an empty public SSID. */
    ESP_RETURN_ON_ERROR(luna_weather_start(weather_update,NULL),TAG,"weather worker failed");
    return xTaskCreate(setup_worker,"luna_wifi_setup",6144,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
