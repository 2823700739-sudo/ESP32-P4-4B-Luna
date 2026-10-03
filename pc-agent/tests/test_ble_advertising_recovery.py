"""Compile actual project advertising functions against explicit native GAP stubs.

This verifies retry/guard behavior, not the C6 controller or Windows adapter.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(shutil.which('gcc'), 'Existing native GCC required')
class AdvertisingRecoveryTests(unittest.TestCase):
    def test_connection_slot_busy_retries_after_callback_and_preserves_active_link(self):
        source = (ROOT/'firmware/luna-panel/main/luna_ble_b0.c').read_text(encoding='utf-8')
        start = source.index('static void advertise(void)\n')
        end = source.index('\nstatic void apply_bond_recovery', start)
        functions = source[start:end]
        preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_FOREVER -1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef struct { int value; } ble_uuid128_t;
struct ble_hs_adv_fields { int flags; const uint8_t *name; int name_len,name_is_complete;
    ble_uuid128_t *uuids128; int num_uuids128,uuids128_is_complete; };
struct ble_gap_adv_params { int conn_mode,disc_mode; };
struct ble_npl_event { int unused; };
struct ble_npl_callout { int unused; };
static struct ble_npl_callout adv_retry;
static ble_uuid128_t service_uuid;
static uint8_t own_addr_type;
static atomic_uint conn_handle=BLE_HS_CONN_HANDLE_NONE;
static bool synced=true,active;
static unsigned attempts,resets,stops,delay;
static int result;
static int gap_event(void *e,void *a){(void)e;(void)a;return 0;}
static bool ble_hs_synced(void){return synced;}
static bool ble_gap_adv_active(void){return active;}
static int ble_gap_adv_set_fields(struct ble_hs_adv_fields *f){assert(f->name_len==4&&!memcmp(f->name,"Luna",4));return 0;}
static int ble_gap_adv_start(int type,void *peer,int duration,struct ble_gap_adv_params *p,int (*cb)(void*,void*),void *arg){
    (void)type;(void)peer;(void)duration;(void)cb;(void)arg;
    assert(p->conn_mode==BLE_GAP_CONN_MODE_UND);attempts++;if(!result)active=true;return result;
}
static unsigned ble_npl_time_ms_to_ticks32(unsigned ms){return ms;}
static void ble_npl_callout_reset(struct ble_npl_callout *c,unsigned ticks){(void)c;resets++;delay=ticks;}
static void ble_npl_callout_stop(struct ble_npl_callout *c){(void)c;stops++;}
static void status(const char *s){assert(s&&*s);}
'''
        checks = r'''
int main(void){
    result=6;advertise();assert(attempts==1&&resets==1&&delay==1000&&!active);
    result=0;retry_advertise(NULL);assert(attempts==2&&active&&stops==1);
    advertise();assert(attempts==2); /* already advertising */
    active=false;conn_handle=7;retry_advertise(NULL);assert(attempts==2); /* active secured link */
    conn_handle=BLE_HS_CONN_HANDLE_NONE;synced=false;retry_advertise(NULL);assert(attempts==2);
    synced=true;retry_advertise(NULL);assert(attempts==3&&active);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='luna-adv-recovery-') as directory:
            src=Path(directory)/'adv.c'; executable=Path(directory)/'adv.exe'
            src.write_text(preamble+functions+checks,encoding='utf-8')
            subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(src),'-o',str(executable)],check=True,capture_output=True)
            subprocess.run([str(executable)],check=True,capture_output=True)

if __name__=='__main__':unittest.main()
