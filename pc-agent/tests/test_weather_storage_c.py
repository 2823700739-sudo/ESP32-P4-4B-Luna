"""Exercise the real provisioning worker with faultable NVS/RPC stubs, not hardware."""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).parent))
from test_weather_config_c import Config

STUB=r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_TIMEOUT 263
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 10
#define WIFI_EVENT_SCAN_DONE 13
#define WIFI_SCAN_TYPE_ACTIVE 0
#define WIFI_EVENT_STA_DISCONNECTED 11
#define IP_EVENT_STA_GOT_IP 12
#define IP_EVENT_STA_LOST_IP 14
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_IF_STA 0
#define WIFI_AUTH_WPA2_PSK 2
#define WIFI_AUTH_OPEN 0
#define WPA3_SAE_PWE_BOTH 0
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(v) (v)
#define CONFIG_LUNA_WIFI_SSID ""
#define CONFIG_LUNA_WIFI_PASSWORD ""
#define CONFIG_LUNA_BLE_B1_MUSIC 1
#define WIFI_MODE_APSTA 3
#define WIFI_IF_AP 1
#define ESP_RETURN_ON_ERROR(expr,tag,msg) do {int rc_=(expr);if(rc_!=0)return rc_;} while(0)
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
typedef int esp_event_base_t;
typedef void *esp_timer_handle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef int nvs_handle_t;
typedef struct {void(*callback)(void*);const char*name;} esp_timer_create_args_t;
typedef struct {int unused;} wifi_init_config_t;
typedef struct {unsigned status;} wifi_event_sta_scan_done_t;
typedef struct {bool show_hidden;int scan_type;struct{struct{unsigned min,max;}active;}scan_time;} wifi_scan_config_t;
typedef struct {uint8_t ssid[33];int rssi,authmode;} wifi_ap_record_t;
int esp_wifi_scan_start(const wifi_scan_config_t*,bool);int esp_wifi_scan_stop(void);
int esp_wifi_scan_get_ap_records(uint16_t*,wifi_ap_record_t*);int esp_wifi_clear_ap_list(void);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t*);
#define WIFI_INIT_CONFIG_DEFAULT() {0}
typedef struct {struct {uint8_t ssid[32],password[64];struct{int authmode;}threshold;int sae_pwe_h2e;}sta;
struct {uint8_t ssid[32],password[64];unsigned ssid_len,channel,max_connection;int authmode;}ap;} wifi_config_t;
int esp_wifi_connect(void);int esp_wifi_stop(void);int esp_wifi_start(void);
int esp_wifi_disconnect(void);void esp_fill_random(void*,size_t);int64_t esp_timer_get_time(void);
int esp_wifi_init(wifi_init_config_t*);int esp_wifi_set_storage(int);int esp_wifi_set_mode(int);int esp_wifi_set_config(int,wifi_config_t*);
int esp_netif_init(void);int esp_event_loop_create_default(void);void*esp_netif_get_handle_from_ifkey(const char*);void*esp_netif_create_default_wifi_sta(void);
void*esp_netif_create_default_wifi_ap(void);
int esp_event_handler_register(int,int,void(*)(void*,int,int32_t,void*),void*);
int esp_timer_start_once(void*,uint64_t);int esp_timer_stop(void*);bool esp_timer_is_active(void*);int esp_timer_create(const esp_timer_create_args_t*,void**);
void*xSemaphoreCreateMutex(void);int xSemaphoreTake(void*,unsigned);int xSemaphoreGive(void*);
void*xQueueCreate(unsigned,unsigned);int xQueueSend(void*,const void*,unsigned);int xQueueReceive(void*,void*,unsigned);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
int nvs_open(const char*,int,nvs_handle_t*);int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);int nvs_commit(nvs_handle_t);void nvs_close(nvs_handle_t);
size_t strlcpy(char*,const char*,size_t);
'''
RUNTIME=r'''
#include "stub.h"
#include <assert.h>
#include <setjmp.h>
#include "luna_ble_wifi.c"
static jmp_buf end_worker;
static luna_weather_config_t stored,pending;
static setup_job_t queued;
static bool has_store,has_queue;
static int nvs_error,rpc_error;
static int64_t test_now;
static int test_mode,portal_running;
static int ap_auth,ap_has_password;
static int scan_fault,scan_calls,scan_cleared,scan_stopped,connect_calls;
static wifi_ap_record_t associated;
static int associated_error;
static int connect_error,in_callback;
static int association_without_ip,disconnect_calls,inject_old_trial;
static bool timer_active;
static uint64_t timer_delay;
int esp_wifi_sta_get_ap_info(wifi_ap_record_t*out){*out=associated;return associated_error;}
int esp_wifi_scan_start(const wifi_scan_config_t*c,bool block){if(block||c->show_hidden)return -2;scan_calls++;return scan_fault;}
int esp_wifi_scan_stop(void){scan_stopped++;return 0;}int esp_wifi_clear_ap_list(void){scan_cleared++;return 0;}
int esp_wifi_scan_get_ap_records(uint16_t*n,wifi_ap_record_t*out){
    if(scan_fault)return scan_fault;
    wifi_ap_record_t records[]={
      {.ssid="Home",.rssi=-30,.authmode=2},{.ssid="Home",.rssi=-40,.authmode=2},
      {.ssid="",.rssi=-45},{.ssid="\xff",.rssi=-50},{.ssid="Home",.rssi=-60,.authmode=0},
      {.ssid="12345678901234567890123456789012",.rssi=-70,.authmode=2}};
    unsigned count=sizeof(records)/sizeof(records[0]);if(*n<count)return -3;
    memcpy(out,records,sizeof(records));*n=count;return 0;
}
static void(*worker_entry)(void*);
int esp_wifi_connect(void){assert(!in_callback);connect_calls++;return association_without_ip?-2:connect_error;}int esp_wifi_stop(void){return 0;}int esp_wifi_start(void){return 0;}
int esp_wifi_disconnect(void){assert(!in_callback);association_without_ip=0;disconnect_calls++;return 0;}void esp_fill_random(void*p,size_t n){memset(p,17,n);}
int64_t esp_timer_get_time(void){return test_now;}
int luna_setup_portal_start(void){portal_running=1;return 0;}void luna_setup_portal_stop(void){portal_running=0;}
void luna_time_set_network_ready(bool value){(void)value;}
int esp_wifi_init(wifi_init_config_t*c){(void)c;return rpc_error;}
int esp_wifi_set_storage(int v){(void)v;return 0;}int esp_wifi_set_mode(int v){test_mode=v;return 0;}
int esp_wifi_set_config(int v,wifi_config_t*c){if(v==WIFI_IF_AP){ap_auth=c->ap.authmode;ap_has_password=c->ap.password[0]!=0;}return 0;}
int esp_netif_init(void){return 0;}int esp_event_loop_create_default(void){return 0;}
void*esp_netif_get_handle_from_ifkey(const char*s){(void)s;return (void*)1;}void*esp_netif_create_default_wifi_sta(void){return(void*)1;}
void*esp_netif_create_default_wifi_ap(void){return(void*)1;}
int esp_event_handler_register(int a,int b,void(*c)(void*,int,int32_t,void*),void*d){(void)a;(void)b;(void)c;(void)d;return 0;}
int esp_timer_start_once(void*a,uint64_t b){(void)a;if(timer_active)return ESP_ERR_INVALID_STATE;timer_active=true;timer_delay=b;return 0;}
int esp_timer_stop(void*a){(void)a;timer_active=false;return 0;}
bool esp_timer_is_active(void*a){(void)a;return timer_active;}int esp_timer_create(const esp_timer_create_args_t*a,void**b){(void)a;*b=(void*)1;timer_active=false;return 0;}
void*xSemaphoreCreateMutex(void){return(void*)1;}int xSemaphoreTake(void*a,unsigned b){(void)a;(void)b;return 1;}
int xSemaphoreGive(void*a){(void)a;return 1;}void*xQueueCreate(unsigned a,unsigned b){(void)a;(void)b;return(void*)1;}
int xQueueSend(void*a,const void*b,unsigned c){(void)a;(void)c;if(has_queue)return 0;memcpy(&queued,b,sizeof(queued));has_queue=true;return 1;}
int xQueueReceive(void*a,void*b,unsigned c){(void)a;(void)c;if(!has_queue)longjmp(end_worker,1);if(inject_old_trial&&queued.trial){atomic_store(&retries,4);atomic_store(&reconnect_requested,true);timer_active=true;timer_delay=1000000;inject_old_trial=0;}memcpy(b,&queued,sizeof(queued));memset(&queued,0,sizeof(queued));has_queue=false;return 1;}
int xTaskCreate(void(*a)(void*),const char*b,unsigned c,void*d,unsigned e,void*f){(void)b;(void)c;(void)d;(void)e;(void)f;worker_entry=a;return 1;}
int nvs_open(const char*a,int b,nvs_handle_t*c){(void)a;if(!b&&!has_store)return 1;*c=1;return 0;}
int nvs_get_blob(nvs_handle_t a,const char*b,void*c,size_t*d){(void)a;(void)b;if(!has_store)return 1;memcpy(c,&stored,sizeof(stored));*d=sizeof(stored);return 0;}
int nvs_set_blob(nvs_handle_t a,const char*b,const void*c,size_t d){(void)a;(void)b;if(d!=sizeof(pending))return 1;memcpy(&pending,c,d);return 0;}
int nvs_commit(nvs_handle_t a){(void)a;if(nvs_error)return nvs_error;stored=pending;has_store=true;return 0;}
void nvs_close(nvs_handle_t a){(void)a;}
int luna_weather_start(luna_weather_update_cb_t cb,void*c){luna_weather_state_t s={0};cb(&s,c);return 0;}
void luna_weather_set_location(const luna_weather_state_t*s){weather_update(s,NULL);}
void luna_weather_set_network_ready(bool b){(void)b;}
size_t strlcpy(char*out,const char*s,size_t n){size_t z=strlen(s);if(n){size_t k=z<n-1?z:n-1;memcpy(out,s,k);out[k]=0;}return z;}
void test_pump(void){if(!setjmp(end_worker))worker_entry(NULL);}
void test_reset(int preserve){if(!preserve){has_store=false;memset(&stored,0,sizeof(stored));}has_queue=false;nvs_error=rpc_error=0;
  setup_mutex=NULL;setup_queue=NULL;recovery_timer=NULL;driver_initialized=wifi_events=ip_events=false;
  test_now=0;portal_running=0;trial_active=ap_committed=false;ap_deadline=ap_close_after=0;memset(&ap,0,sizeof(ap));atomic_store(&ap_request,0);atomic_store(&station_enabled,false);
  memset(&scan,0,sizeof(scan));atomic_store(&scan_busy,false);atomic_store(&scan_done,0);scan_started=false;scan_next=scan_deadline=0;
  scan_fault=scan_calls=scan_cleared=scan_stopped=connect_calls=0;
  connect_error=in_callback=0;timer_active=false;timer_delay=0;atomic_store(&reconnect_requested,false);atomic_store(&retries,0);
  association_without_ip=disconnect_calls=inject_old_trial=0;atomic_store(&reconnect_reassociate,false);
  memset(&associated,0,sizeof(associated));associated_error=0;trial_deadline=0;memset(trial_ssid,0,sizeof(trial_ssid));
  memset(&setup_status,0,sizeof(setup_status));atomic_store(&online,false);atomic_store(&configured,false);atomic_store(&available,false);atomic_store(&cached,false);atomic_store(&reconfiguring,false);
  luna_ble_wifi_start();test_pump();}
void test_faults(int a,int b){nvs_error=a;rpc_error=b;}
int test_state(void){return (int)setup_status.state;}int test_saved(void){return setup_status.saved;}
int test_has_store(void){return has_store;}const char*test_stored_id(void){return stored.setup_id;}
const char*test_status_id(void){return setup_status.setup_id;}
void test_ap_tick(int seconds){test_now+=(int64_t)seconds*1000000;service_ap();}
int test_ap_active(void){return ap.active;}int test_portal_running(void){return portal_running;}
int test_network_enabled(void){return atomic_load(&station_enabled);}int test_trial(void){return trial_active;}
int test_wifi_mode(void){return test_mode;}
void test_reset_driver(void){driver_initialized=false;}
int test_ap_auth(void){return ap_auth;}int test_ap_has_password(void){return ap_has_password;}
void test_scan_tick(int seconds,int done){test_now+=(int64_t)seconds*1000000;
  if(done){wifi_event_sta_scan_done_t event={.status=done==1?0:1};network_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&event);}service_scan();}
void test_scan_fault(int error){scan_fault=error;}int test_scan_state(void){return scan.state;}
int test_scan_count(void){return scan.count;}int test_scan_calls(void){return scan_calls;}
int test_scan_cleared(void){return scan_cleared;}int test_scan_stopped(void){return scan_stopped;}
int test_connect_calls(void){return connect_calls;}
void test_connect_error(int error){connect_error=error;}
int test_online(void){return atomic_load(&online);}int test_timer_active(void){return timer_active;}
int test_timer_delay(void){return (int)timer_delay;}
void test_associated_without_ip(void){association_without_ip=1;}
int test_disconnect_calls(void){return disconnect_calls;}void test_inject_old_trial(void){inject_old_trial=1;}
int test_retries(void){return atomic_load(&retries);}
void test_reconnect_tick(void){service_reconnect();}
void test_fire_recovery(void){timer_active=false;in_callback=1;recover(NULL);in_callback=0;}
void test_event(int event){in_callback=1;network_event(NULL,event==IP_EVENT_STA_GOT_IP||event==IP_EVENT_STA_LOST_IP?IP_EVENT:WIFI_EVENT,event,NULL);in_callback=0;}
void test_trial_tick(int seconds,const char*ssid,int ip,int error){
  test_now+=(int64_t)seconds*1000000;memset(&associated,0,sizeof(associated));
  if(ssid)strlcpy((char*)associated.ssid,ssid,sizeof(associated.ssid));
  associated_error=error;
  if(ip)network_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);else network_ready(false);
  service_trial();
}
int test_trial_state(void){return ap.trial_state;}int test_trial_error(void){return ap.trial_error;}
const char*test_trial_id(void){return ap.trial_id;}
const char*test_scan_name(int i){return scan.networks[i].ssid;}
int test_scan_secure(int i){return scan.networks[i].secured;}
'''

@unittest.skipUnless(shutil.which('gcc'),'existing GCC required')
class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-weather-storage-');folder=Path(cls.temp.name)
        for header in ['esp_check.h','esp_event.h','esp_log.h','esp_netif.h','esp_timer.h','esp_wifi.h','nvs.h','esp_err.h',
                       'esp_random.h','freertos/FreeRTOS.h','freertos/queue.h','freertos/semphr.h','freertos/task.h']:
            p=folder/header;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
        (folder/'stub.h').write_text(STUB);(folder/'runtime.c').write_text(RUNTIME)
        cjson=ROOT/'firmware/luna-panel/managed_components/espressif__cjson/cJSON';main=ROOT/'firmware/luna-panel/main'
        dll=folder/'storage.dll'
        result=subprocess.run(['gcc','-std=c11','-shared','-O1','-Wall','-Wextra','-Werror','-DCJSON_HIDE_SYMBOLS',
                        '-I',str(folder),'-I',str(main),'-I',str(cjson),str(folder/'runtime.c'),
                        str(main/'luna_weather_config.c'),str(cjson/'cJSON.c'),'-o',str(dll)],capture_output=True)
        if result.returncode:raise AssertionError(result.stderr.decode(errors='replace'))
        cls.lib=C.CDLL(str(dll));cls.lib.test_reset.argtypes=[C.c_int];cls.lib.test_faults.argtypes=[C.c_int,C.c_int]
        cls.lib.luna_ble_wifi_configure.argtypes=[C.POINTER(Config)];cls.lib.luna_ble_wifi_configure.restype=C.c_bool
        cls.lib.luna_ble_wifi_try_network.argtypes=[C.POINTER(Config)];cls.lib.luna_ble_wifi_try_network.restype=C.c_bool
        cls.lib.luna_setup_ap_request.argtypes=[C.c_bool]
        cls.lib.luna_setup_scan_request.restype=C.c_bool
        cls.lib.test_scan_name.restype=C.c_char_p
        cls.lib.test_trial_tick.argtypes=[C.c_int,C.c_char_p,C.c_int,C.c_int]
        cls.lib.test_trial_id.restype=C.c_char_p
        cls.lib.test_stored_id.restype=C.c_char_p;cls.lib.test_status_id.restype=C.c_char_p
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def setUp(self):self.lib.test_reset(0)
    def config(self, token=b'1'*32):
        return Config(1,token,b'Test network',b'example-only',b'City',1,2)
    def save(self,token=b'1'*32):
        c=self.config(token);self.assertTrue(self.lib.luna_ble_wifi_configure(C.byref(c)));self.lib.test_pump()
    def test_empty_public_boot_can_provision_then_reload_after_restart(self):
        self.assertEqual(self.lib.test_state(),0);self.assertFalse(self.lib.test_has_store())
        self.save();self.assertEqual(self.lib.test_state(),2);self.assertTrue(self.lib.test_saved())
        self.lib.test_reset(1);self.assertEqual(self.lib.test_status_id(),b'1'*32);self.assertTrue(self.lib.test_saved())
    def test_commit_failure_keeps_previous_committed_settings(self):
        self.save();self.lib.test_faults(257,0);self.save(b'2'*32)
        self.assertEqual(self.lib.test_state(),4);self.assertFalse(self.lib.test_saved())
        self.assertEqual(self.lib.test_stored_id(),b'1'*32)
        self.lib.test_reset(1);self.assertEqual(self.lib.test_status_id(),b'1'*32)
    def test_failed_c6_init_is_saved_but_reports_apply_failure(self):
        self.lib.test_reset(0);self.lib.test_faults(0,-1)
        # Force the existing driver to be unavailable, including when first-boot AP ran.
        self.lib.test_reset_driver();self.save()
        self.assertEqual(self.lib.test_state(),3);self.assertTrue(self.lib.test_saved())
    def test_second_inflight_request_is_rejected_without_overwrite(self):
        a,b=self.config(),self.config(b'2'*32)
        self.assertTrue(self.lib.luna_ble_wifi_configure(C.byref(a)))
        self.assertFalse(self.lib.luna_ble_wifi_configure(C.byref(b)))
        self.lib.test_pump();self.assertEqual(self.lib.test_stored_id(),b'1'*32)
    def test_first_boot_opens_password_free_ap_and_times_out(self):
        self.assertTrue(self.lib.test_ap_active());self.assertTrue(self.lib.test_portal_running())
        self.assertEqual(self.lib.test_wifi_mode(),3)
        self.assertEqual(self.lib.test_ap_auth(),0);self.assertEqual(self.lib.test_ap_has_password(),0)
        self.lib.test_ap_tick(601)
        self.assertFalse(self.lib.test_ap_active());self.assertFalse(self.lib.test_portal_running())
    def test_trial_never_commits_and_timeout_discards_unsaved_network(self):
        config=self.config();self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(config)));self.lib.test_pump()
        self.assertTrue(self.lib.test_trial());self.assertFalse(self.lib.test_has_store())
        self.lib.test_ap_tick(601)
        self.assertFalse(self.lib.test_network_enabled());self.assertFalse(self.lib.test_saved())
    def test_trial_then_cancel_restores_previous_committed_network(self):
        self.save();config=self.config(b'2'*32)
        self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(config)));self.lib.test_pump()
        self.lib.luna_setup_ap_request(False);self.lib.test_ap_tick(1)
        self.assertEqual(self.lib.test_stored_id(),b'1'*32);self.assertTrue(self.lib.test_network_enabled())
    def test_restart_with_saved_settings_does_not_open_ap(self):
        self.save();self.lib.test_reset(1)
        self.assertFalse(self.lib.test_ap_active());self.assertFalse(self.lib.test_portal_running())
        self.lib.luna_setup_ap_request(True);self.lib.test_ap_tick(1)
        self.assertTrue(self.lib.test_ap_active())
    def test_trial_queued_with_old_online_network_cannot_report_success(self):
        self.lib.test_trial_tick(0,b'Old network',1,0)
        c=self.config();self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(c)))
        self.lib.test_trial_tick(0,b'Old network',1,0)
        self.assertEqual(self.lib.test_trial_state(),1)
        self.assertEqual(self.lib.test_trial_id(),b'1'*32)
        self.assertFalse(self.lib.luna_setup_scan_request())
        self.assertFalse(self.lib.luna_ble_wifi_configure(C.byref(c)))
        self.lib.test_pump();self.lib.test_trial_tick(1,b'Old network',1,0)
        self.assertEqual(self.lib.test_trial_state(),1)
        self.lib.test_trial_tick(1,b'Test network',1,0)
        self.assertEqual(self.lib.test_trial_state(),2)
        self.assertFalse(self.lib.test_has_store())
        self.assertTrue(self.lib.luna_ble_wifi_configure(C.byref(c)))
    def test_trial_requires_ip_and_matching_association_then_has_bounded_failure(self):
        c=self.config();self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(c)));self.lib.test_pump()
        self.lib.test_trial_tick(1,b'Test network',0,0);self.assertEqual(self.lib.test_trial_state(),1)
        self.lib.test_trial_tick(1,b'Test network',1,-1);self.assertEqual(self.lib.test_trial_state(),1)
        self.lib.test_trial_tick(29,b'Wrong network',1,0)
        self.assertEqual(self.lib.test_trial_state(),3);self.assertEqual(self.lib.test_trial_error(),263)
        self.lib.test_trial_tick(1,b'Test network',1,0);self.assertEqual(self.lib.test_trial_state(),3)
        c=self.config(b'2'*32);self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(c)))
        self.assertEqual(self.lib.test_trial_id(),b'2'*32);self.assertEqual(self.lib.test_trial_state(),1)
    def test_trial_radio_failure_and_close_clear_current_result(self):
        self.lib.test_reset_driver();self.lib.test_faults(0,-1)
        c=self.config();self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(c)));self.lib.test_pump()
        self.assertEqual(self.lib.test_trial_state(),3);self.assertEqual(self.lib.test_trial_error(),-1)
        self.lib.luna_setup_ap_request(False);self.lib.test_ap_tick(1)
        self.assertEqual(self.lib.test_trial_state(),0);self.assertEqual(self.lib.test_trial_id(),b'')
    def test_scan_is_worker_owned_deduplicated_and_does_not_join_or_save(self):
        before=self.lib.test_connect_calls()
        self.assertTrue(self.lib.luna_setup_scan_request());self.assertEqual(self.lib.test_scan_calls(),0)
        self.assertFalse(self.lib.luna_setup_scan_request())
        c=self.config();self.assertFalse(self.lib.luna_ble_wifi_configure(C.byref(c)))
        self.lib.test_scan_tick(0,0);self.assertEqual(self.lib.test_scan_calls(),1)
        self.lib.test_scan_tick(1,1)
        self.assertEqual(self.lib.test_scan_state(),2);self.assertEqual(self.lib.test_scan_count(),3)
        self.assertEqual(self.lib.test_scan_name(0),b'Home');self.assertEqual(self.lib.test_scan_secure(1),0)
        self.assertEqual(self.lib.test_scan_name(2),b'12345678901234567890123456789012')
        self.assertFalse(self.lib.test_has_store());self.assertEqual(self.lib.test_connect_calls(),before)
        self.assertTrue(self.lib.test_ap_active());self.assertFalse(self.lib.luna_setup_scan_request())
        self.lib.test_scan_tick(5,0);self.assertTrue(self.lib.luna_setup_scan_request())
    def test_scan_error_and_timeout_release_busy_for_configuration(self):
        self.lib.test_scan_fault(-1);self.assertTrue(self.lib.luna_setup_scan_request());self.lib.test_scan_tick(0,0)
        self.assertEqual(self.lib.test_scan_state(),3);self.assertEqual(self.lib.test_scan_cleared(),1)
        self.lib.test_scan_fault(0);self.lib.test_scan_tick(5,0)
        self.assertTrue(self.lib.luna_setup_scan_request());self.lib.test_scan_tick(0,0);self.lib.test_scan_tick(13,0)
        self.assertEqual(self.lib.test_scan_state(),3);self.assertEqual(self.lib.test_scan_stopped(),1)
        c=self.config();self.assertTrue(self.lib.luna_ble_wifi_configure(C.byref(c)))
    def test_closing_ap_cancels_scan_and_forbids_new_requests(self):
        self.assertTrue(self.lib.luna_setup_scan_request());self.lib.test_scan_tick(0,0)
        self.lib.luna_setup_ap_request(False);self.lib.test_ap_tick(1)
        self.assertEqual(self.lib.test_scan_stopped(),1);self.assertEqual(self.lib.test_scan_count(),0)
        self.assertFalse(self.lib.luna_setup_scan_request())
    def test_scan_get_records_failure_clears_driver_list(self):
        self.assertTrue(self.lib.luna_setup_scan_request());self.lib.test_scan_tick(0,0)
        self.lib.test_scan_fault(-1);self.lib.test_scan_tick(1,1)
        self.assertEqual(self.lib.test_scan_state(),3);self.assertEqual(self.lib.test_scan_cleared(),1)
    def test_connect_rpc_failure_retries_only_on_worker_and_retains_watchdog(self):
        self.save();self.lib.test_connect_error(-1)
        before=self.lib.test_connect_calls();self.lib.test_event(10)
        self.assertEqual(self.lib.test_connect_calls(),before)
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+1)
        self.assertTrue(self.lib.test_timer_active());self.assertEqual(self.lib.test_timer_delay(),30000000)
        self.lib.test_fire_recovery();self.assertEqual(self.lib.test_connect_calls(),before+1)
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+2)
        self.assertTrue(self.lib.test_timer_active())
        self.lib.test_event(12);self.assertTrue(self.lib.test_online());self.assertFalse(self.lib.test_timer_active())
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+2)
    def test_disconnect_and_lost_ip_clear_online_without_blocking_event_loop(self):
        self.save();self.lib.test_event(12);self.assertTrue(self.lib.test_online())
        before=self.lib.test_connect_calls();self.lib.test_event(14)
        self.assertFalse(self.lib.test_online());self.assertEqual(self.lib.test_connect_calls(),before)
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+1)
        self.lib.test_event(11);self.assertEqual(self.lib.test_connect_calls(),before+1)
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+2)
    def test_cancel_trial_ignores_late_ip_and_pending_recovery(self):
        c=self.config();self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(c)));self.lib.test_pump()
        self.lib.luna_setup_ap_request(False);self.lib.test_ap_tick(1)
        self.assertFalse(self.lib.test_network_enabled());self.lib.test_event(12)
        self.assertFalse(self.lib.test_online())
        before=self.lib.test_connect_calls();self.lib.test_fire_recovery();self.lib.test_reconnect_tick()
        self.assertEqual(self.lib.test_connect_calls(),before)
    def test_reconnect_defers_during_scan_and_resumes_once(self):
        self.save();self.lib.test_event(12);self.assertTrue(self.lib.luna_setup_scan_request())
        self.lib.test_event(14);before=self.lib.test_connect_calls();self.lib.test_fire_recovery()
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before)
        self.lib.test_scan_tick(0,0);self.lib.test_scan_tick(1,1)
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+1)
    def test_lost_ip_and_watchdog_reassociate_instead_of_busy_connect_loop(self):
        self.save();self.lib.test_event(12);self.lib.test_associated_without_ip()
        before=self.lib.test_disconnect_calls();self.lib.test_event(14);self.lib.test_reconnect_tick()
        self.assertEqual(self.lib.test_disconnect_calls(),before+1);self.assertTrue(self.lib.test_timer_active())
        self.lib.test_associated_without_ip();self.lib.test_fire_recovery();self.lib.test_reconnect_tick()
        self.assertEqual(self.lib.test_disconnect_calls(),before+2)
        self.lib.test_event(12);self.assertTrue(self.lib.test_online());self.assertFalse(self.lib.test_timer_active())
    def test_new_trial_clears_previous_network_backoff_and_watchdog(self):
        c=self.config();self.lib.test_inject_old_trial()
        self.assertTrue(self.lib.luna_ble_wifi_try_network(C.byref(c)));self.lib.test_pump()
        self.assertEqual(self.lib.test_retries(),0)
        self.assertEqual(self.lib.test_timer_delay(),30000000)
        before=self.lib.test_connect_calls();self.lib.test_event(11);self.lib.test_reconnect_tick()
        self.assertEqual(self.lib.test_retries(),1);self.assertEqual(self.lib.test_connect_calls(),before+1)
        self.lib.test_reconnect_tick();self.assertEqual(self.lib.test_connect_calls(),before+1)
