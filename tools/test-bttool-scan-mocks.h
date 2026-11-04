/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SCAN_MOCKS_H
#define SCAN_MOCKS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define PRINT(...) do { printf(__VA_ARGS__); puts(""); } while (0)
#define PRINT_ADDR(...) ((void)0)
#define CMD_OK 0
#define CMD_ERROR -9
#define CMD_INVALID_OPT -4
#define CMD_USAGE_FAULT -8
typedef int bt_adapter_state_t;
enum { BT_ADAPTER_STATE_OFF, BT_ADAPTER_STATE_BLE_TURNING_ON, BT_ADAPTER_STATE_BLE_ON,
       BT_ADAPTER_STATE_TURNING_ON, BT_ADAPTER_STATE_ON, BT_ADAPTER_STATE_TURNING_OFF,
       BT_ADAPTER_STATE_BLE_TURNING_OFF };
enum { BT_SCAN_MODE_LOW_POWER, BT_SCAN_MODE_BALANCED, BT_SCAN_MODE_LOW_LATENCY };
enum { BT_LE_SCAN_TYPE_PASSIVE, BT_LE_SCAN_TYPE_ACTIVE, BT_LE_1M_PHY, BT_LE_2M_PHY, BT_LE_CODED_PHY };
enum { BT_LE_SCAN_POLICY_ACCEPT_ALL, BT_LE_SCAN_POLICY_ONLY_WHITE_LIST,
       BT_LE_SCAN_POLICY_ACCEPT_ALL_AND_RPA, BT_LE_SCAN_POLICY_ONLY_WHITE_LIST_AND_RPA };
typedef struct { bool active; unsigned int uuids[1]; } ble_scan_filter_t;
typedef struct { int scan_mode, duration, scan_type, scan_phy; struct { int policy; } policy; int legacy; } ble_scan_settings_t;
typedef struct { int addr, addr_type, rssi, adv_type; const unsigned char *adv_data; int length; } ble_scan_result_t;
typedef struct { unsigned int id; } bt_scanner_t;
typedef struct { size_t size; void (*result)(bt_scanner_t *, ble_scan_result_t *);
  void (*start)(bt_scanner_t *, uint8_t); void (*stop)(bt_scanner_t *); } scanner_callbacks_t;
typedef struct { const char *cmd; int (*func)(void *,int,char **); int unused; const char *help; } bt_command_t;
static bt_adapter_state_t mock_state = BT_ADAPTER_STATE_BLE_ON;
static int start_calls, stop_calls;
static bool fail_start;
static bool early_callback;
static void schedule_start_callback(bt_scanner_t *scanner);
static const scanner_callbacks_t *mock_callbacks;
static bt_adapter_state_t bt_adapter_get_state(void *handle) { (void)handle; return mock_state; }
static void advertiser_data_dump(uint8_t *data,int length,void *unused) { (void)data; (void)length; (void)unused; }
static bt_scanner_t *bt_le_start_scan(void *handle,const scanner_callbacks_t *callbacks)
{
  (void)handle; start_calls++; mock_callbacks=callbacks;
  bt_scanner_t *scanner=fail_start ? NULL : calloc(1,sizeof(bt_scanner_t));
  if(scanner && early_callback) schedule_start_callback(scanner);
  return scanner;
}
static bt_scanner_t *bt_le_start_scan_settings(void *h,ble_scan_settings_t *s,const scanner_callbacks_t *c)
{ (void)s; return bt_le_start_scan(h,c); }
static bt_scanner_t *bt_le_start_scan_with_filters(void *h,ble_scan_settings_t *s,ble_scan_filter_t *f,const scanner_callbacks_t *c)
{ (void)f; return bt_le_start_scan_settings(h,s,c); }
static void bt_le_stop_scan(void *handle,bt_scanner_t *scanner)
{ (void)handle; assert(scanner); stop_calls++; }
static int execute_command_in_table_offset(void *h,bt_command_t *t,size_t n,int argc,char **argv,int offset)
{
  (void)offset;
  for(size_t i=0;i<n;i++) if(!strcmp(t[i].cmd,argv[0])) return t[i].func(h,argc,argv);
  return CMD_ERROR;
}
#endif
