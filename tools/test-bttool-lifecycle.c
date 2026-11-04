/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#define CONFIG_BLUETOOTH_BLE_SCAN 1
#define CONFIG_BLUETOOTH_GATT_CLIENT 1
#define CONFIG_BLUETOOTH_SERVICE_LOOP_THREAD_PRIORITY 100
#define CMD_OK 0
#define CMD_ERROR -9
#define PRINT(...) do { printf(__VA_ARGS__); puts(""); } while (0)
typedef int bt_adapter_state_t;
enum { BT_ADAPTER_STATE_OFF, BT_ADAPTER_STATE_BLE_TURNING_ON, BT_ADAPTER_STATE_BLE_ON,
       BT_ADAPTER_STATE_TURNING_ON, BT_ADAPTER_STATE_ON, BT_ADAPTER_STATE_TURNING_OFF,
       BT_ADAPTER_STATE_BLE_TURNING_OFF };
typedef int bt_status_t;
enum { BT_STATUS_SUCCESS, BT_STATUS_FAIL, BT_STATUS_NOT_READY, BT_STATUS_NOMEM,
       BT_STATUS_BUSY, BT_STATUS_DONE };
typedef int bt_io_capability_t;
typedef int bt_scan_mode_t;
#define BT_BR_SCAN_TYPE_INTERLACED 1
typedef struct { int unused; } bttool_t;
static bool g_cmd_had_inited, g_tool_exiting;
static pthread_mutex_t g_tool_lock=PTHREAD_MUTEX_INITIALIZER;
static void *g_bttool_ins, *adapter_callback;
static const int g_adapter_cbs=1;
static int mock_state, mock_status, enables, disables, initializes, uninitializes, gatt_inits, gatt_uninits;
static int waited, deleted;
static bt_adapter_state_t bt_adapter_get_state(void *h) { (void)h; return mock_state; }
static bt_status_t bt_adapter_enable(void *h) { (void)h; enables++; return mock_status; }
static bt_status_t bt_adapter_disable_safe(void *h) { (void)h; disables++; return mock_status; }
static void scan_command_init(void *h) { (void)h; initializes++; }
static void scan_command_uninit(void *h) { (void)h; uninitializes++; }
static void gattc_command_init(void *h) { (void)h; gatt_inits++; }
static void gattc_command_uninit(void *h) { (void)h; gatt_uninits++; }
static int scan_command_wait_stopped(unsigned int ms) { assert(ms==2000); waited++; return CMD_OK; }
static void bt_adapter_get_name(void *h,char *name,int n) { (void)h; (void)n; name[0]=0; }
static int bt_adapter_get_io_capability(void *h) { (void)h; return 0; }
static uint32_t bt_adapter_get_device_class(void *h) { (void)h; return 0; }
static int bt_adapter_get_scan_mode(void *h) { (void)h; return 0; }
static void bt_adapter_le_enable_key_derivation(void *h,bool a,bool b) { (void)h; (void)a; (void)b; }
static void bt_adapter_set_page_scan_parameters(void *h,int a,int b,int c) { (void)h; (void)a; (void)b; (void)c; }
static void *bluetooth_create_instance(void) { return (void *)(uintptr_t)1; }
static void *bt_adapter_register_callback(void *h,const void *c) { (void)h; (void)c; return (void *)(uintptr_t)2; }
static void bt_adapter_unregister_callback(void *h,void *c) { (void)h; (void)c; }
static void bluetooth_delete_instance(void *h) { assert(h); deleted++; }
/* Do not alter host scheduling for this isolated source-level test. */
#define pthread_setschedprio(t,p) ((void)(t),(void)(p))
#include "tool-extracted.h"

typedef struct { int unused; } state_machine_t;
typedef struct { int id; } state_t;
static const state_t turning_on_state={1}, ble_turning_off_state={2};
enum { SYS_TURN_ON, TURN_ON_BLE, SYS_TURN_OFF_SAFE, SYS_TURN_OFF, TURN_OFF_BLE };
static bool supports_classic;
static const state_t *transition;
#define ADAPTER_DBG_EVENT(sm,event) ((void)(sm),(void)(event))
static bool adapter_is_support_bredr(void) { return supports_classic; }
static void hsm_transition_to(state_machine_t *sm,const state_t *to) { (void)sm; transition=to; }
#include "state-extracted.h"

typedef unsigned int bt_controller_id_t;
typedef struct { void (*func)(void *); } sal_adapter_req_t;
#define STACK_CALL(x) x
#define BLE_STACK_STATE_OFF 0
#define BT_LOGE(...) PRINT(__VA_ARGS__)
static int off_notifications, disable_error, queue_status;
static bool ready=true;
static sal_adapter_req_t request, *pending_request;
static void zblue_unregister_callback(void) {}
static int bt_disable(void) { return disable_error; }
static void adapter_on_adapter_state_changed(int state) { assert(state==0); off_notifications++; }
static bool bt_is_ready(void) { return ready; }
static sal_adapter_req_t *sal_adapter_req(unsigned int id,void *data,void (*fn)(void *))
{ (void)id; (void)data; request.func=fn; return &request; }
static bt_status_t sal_send_req(sal_adapter_req_t *req) { pending_request=req; return queue_status; }
#include "sal-extracted.h"
int main(void)
{
    bttool_t tool={0};
    mock_state=BT_ADAPTER_STATE_BLE_ON;
    assert(bttool_ins_init(&tool)==0 && initializes==1 && gatt_inits==1);
    on_adapter_state_changed_cb(NULL,BT_ADAPTER_STATE_BLE_ON);
    assert(initializes==1 && gatt_inits==1); /* idempotent */
    assert(enable_cmd(g_bttool_ins,0,NULL)==CMD_OK && enables==0);
    mock_state=BT_ADAPTER_STATE_BLE_TURNING_ON;
    assert(enable_cmd(g_bttool_ins,0,NULL)==CMD_ERROR && enables==0);
    mock_state=BT_ADAPTER_STATE_OFF; mock_status=BT_STATUS_FAIL;
    assert(enable_cmd(g_bttool_ins,0,NULL)==CMD_ERROR && enables==1);
    mock_status=BT_STATUS_SUCCESS;
    assert(enable_cmd(g_bttool_ins,0,NULL)==CMD_OK && enables==2);
    assert(disable_cmd(g_bttool_ins,0,NULL)==CMD_OK && disables==0);
    mock_state=BT_ADAPTER_STATE_BLE_ON; mock_status=BT_STATUS_FAIL;
    assert(disable_cmd(g_bttool_ins,0,NULL)==CMD_ERROR && disables==1);
    mock_status=BT_STATUS_SUCCESS;
    assert(disable_cmd(g_bttool_ins,0,NULL)==CMD_OK && disables==2);
    on_adapter_state_changed_cb(NULL,BT_ADAPTER_STATE_BLE_TURNING_OFF);
    assert(!g_cmd_had_inited && gatt_uninits==1);
    on_adapter_state_changed_cb(NULL,BT_ADAPTER_STATE_OFF);
    assert(gatt_uninits==1);
    mock_state=BT_ADAPTER_STATE_BLE_ON;
    on_adapter_state_changed_cb(NULL,mock_state);
    assert(initializes==2 && gatt_inits==2);
    bttool_ins_uninit(&tool);
    assert(waited==1 && deleted==1 && !g_bttool_ins && !g_cmd_had_inited);
    bt_tool_init(NULL); assert(initializes==2); /* late callback cannot resurrect */
    assert(bttool_ins_init(&tool)==0 && initializes==3); /* fresh reentry */
    bttool_ins_uninit(&tool);
    state_machine_t sm={0};
    supports_classic=false; transition=NULL;
    assert(ble_on_process_event(&sm,SYS_TURN_ON,NULL) && !transition);
    assert(ble_on_process_event(&sm,TURN_ON_BLE,NULL) && !transition);
    assert(ble_on_process_event(&sm,SYS_TURN_OFF_SAFE,NULL) && transition==&ble_turning_off_state);
    supports_classic=true; transition=NULL;
    assert(ble_on_process_event(&sm,SYS_TURN_ON,NULL) && transition==&turning_on_state);
    assert(!ble_on_process_event(&sm,999,NULL));
    assert(bt_sal_le_disable(0)==BT_STATUS_SUCCESS && pending_request && off_notifications==0);
    pending_request->func(NULL); assert(off_notifications==1); /* OFF only after close */
    disable_error=-5; le_disable(NULL); assert(off_notifications==1);
    queue_status=BT_STATUS_NOMEM;
    assert(bt_sal_le_disable(0)==BT_STATUS_NOMEM && off_notifications==1);
    ready=false;
    assert(bt_sal_le_disable(0)==BT_STATUS_SUCCESS && off_notifications==2);
    puts("BLE-only init/exit/reentry/API errors/state machine/SAL completion tests: PASS");
    return 0;
}
