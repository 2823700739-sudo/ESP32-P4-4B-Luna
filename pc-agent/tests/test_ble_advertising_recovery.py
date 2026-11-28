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
    def test_physical_answers_are_bound_to_displayed_connection_generation(self):
        source=(ROOT/'firmware/luna-panel/main/luna_ble_b0.c').read_text(encoding='utf-8')
        start=source.index('static void pairing_answer(')
        end=source.index('\n#ifndef CONFIG_LUNA_BLE_B1_MUSIC',start)
        preamble=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>
struct ble_npl_event {int unused;};
static struct ble_npl_event pair_event,recovery_event;
static atomic_bool pairing,recovery_pending;
static atomic_int pair_answer=-1;
static atomic_uint pair_generation,answer_generation,recovery_generation,recovery_answer_generation;
static atomic_uint pair_code,pair_started,pair_sequence;
static atomic_bool recovery_accept,recovery_action_pending;
static uint16_t pair_conn;
static int64_t esp_timer_get_time(void){return 1000000;}
static bool inject_change;
static unsigned read_code(void){
 unsigned old=atomic_load_explicit(&pair_code,memory_order_seq_cst);
 if(inject_change){inject_change=false;atomic_fetch_add(&pair_sequence,1);atomic_store(&pair_code,654321);atomic_fetch_add(&pair_generation,1);atomic_fetch_add(&pair_sequence,1);}
 return old;
}
#undef atomic_load
#define atomic_load(object) ({__auto_type obj_=(object);__auto_type value_=atomic_load_explicit(obj_,memory_order_seq_cst);if((void*)obj_==(void*)&pair_code)(void)read_code();value_;})
static unsigned queued;
static void *nimble_port_get_dflt_eventq(void){return 0;}
static void ble_npl_eventq_put(void*q,struct ble_npl_event*e){(void)q;(void)e;queued++;}
'''
        checks=r'''
int main(void){
 publish_pairing(7,123456);pairing_view_t view;assert(read_pairing_view(&view)&&view.code==123456&&view.generation==1);
 publish_pairing(7,222222);assert(read_pairing_view(&view)&&view.generation==2);
 pairing_answer(true,1);assert(queued==0&&pair_answer==-1);
 inject_change=true;assert(!read_pairing_view(&view));
 assert(read_pairing_view(&view)&&view.code==654321&&view.generation==3);
 pairing=true;pair_generation=10;
 pairing_answer(true,9);assert(queued==0&&pair_answer==-1);
 pairing_answer(true,10);assert(queued==1&&pair_answer==1&&answer_generation==10);
 pairing_answer(false,10);assert(queued==1);
 pairing=false;pair_answer=-1;recovery_pending=true;recovery_generation=21;
 recovery_answer(20,true);assert(queued==1);
 recovery_answer(21,false);assert(queued==2&&recovery_answer_generation==21&&!recovery_accept);
 pairing=true;recovery_answer(21,true);assert(queued==2);
 return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='luna-pair-generation-')as directory:
            src=Path(directory)/'pair.c';exe=Path(directory)/'pair.exe'
            snapshot=source[source.index('typedef struct {bool pending;'):source.index('static bool secure(')]
            src.write_text(preamble+snapshot+source[start:end]+checks,encoding='utf-8')
            result=subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            subprocess.run([str(exe)],check=True,capture_output=True)

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
#define set_link_phase(value) ((void)0)
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
