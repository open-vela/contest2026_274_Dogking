/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "scan-source.h"
static pthread_t notification;
static uint8_t early_status;
static void *notify_start(void *data)
{
    mock_callbacks->start(data,early_status);
    if(early_status) free(data); /* framework owns failed-start release */
    return NULL;
}
static void schedule_start_callback(bt_scanner_t *scanner)
{
    assert(pthread_create(&notification,NULL,notify_start,scanner)==0);
}
static void complete_stop(bt_scanner_t *scanner)
{
    mock_callbacks->stop(scanner);
    free(scanner); /* Same ownership as the official IPC dispatcher. */
}
int main(void)
{
    char *start[] = {"start",NULL};
    mock_state=BT_ADAPTER_STATE_OFF;
    assert(start_scan_cmd(NULL,1,start)==CMD_ERROR && start_calls==0);
    mock_state=BT_ADAPTER_STATE_BLE_ON;
    assert(start_scan_cmd(NULL,1,start)==CMD_OK && g_scanner);
    bt_scanner_t *first=g_scanner;
    on_scan_start_status_cb(first,0);
    assert(start_scan_cmd(NULL,1,start)==CMD_ERROR && start_calls==1);
    assert(scan_command_init(NULL)==0 && g_scanner==first); /* no erased ownership */
    assert(stop_scan_cmd(NULL,0,NULL)==CMD_OK && g_scan_stopping && g_scanner==first);
    assert(stop_scan_cmd(NULL,0,NULL)==CMD_OK && stop_calls==1);
    assert(scan_command_wait_stopped(2)==CMD_ERROR); /* bounded, not false idle */
    assert(start_scan_cmd(NULL,1,start)==CMD_ERROR);
    complete_stop(first);
    assert(scan_command_wait_stopped(2)==CMD_OK && !g_scanner && !g_scan_stopping);
    assert(stop_scan_cmd(NULL,0,NULL)==CMD_OK && stop_calls==1);
    assert(start_scan_cmd(NULL,1,start)==CMD_OK);
    first=g_scanner;
    on_scan_start_status_cb(first,1);
    free(first);
    assert(!g_scanner && !g_scan_stopping);
    fail_start=true;
    assert(start_scan_cmd(NULL,1,start)==CMD_ERROR && !g_scanner);
    fail_start=false;
    early_callback=true;
    early_status=1;
    for(int i=0;i<20;i++) {
        assert(start_scan_cmd(NULL,1,start)==CMD_OK);
        assert(pthread_join(notification,NULL)==0);
        assert(!g_scanner && !g_scan_stopping);
    }
    early_status=0;
    assert(start_scan_cmd(NULL,1,start)==CMD_OK);
    assert(pthread_join(notification,NULL)==0);
    first=g_scanner;
    scan_command_uninit(NULL);
    complete_stop(first);
    early_callback=false;
    for(int i=0;i<100;i++) {
        assert(start_scan_cmd(NULL,1,start)==CMD_OK);
        first=g_scanner;
        scan_command_uninit(NULL); /* quit/disable requests stop, never free */
        assert(g_scanner==first && g_scan_stopping);
        complete_stop(first);
        assert(scan_command_wait_stopped(2)==CMD_OK);
    }
    char *dump[]={"dump",NULL};
    assert(scan_command_exec(NULL,1,dump)==CMD_OK);
    puts("scanner ownership/early callbacks/start-failure/duplicate-stop/timeout/100-cycle tests: PASS");
    return 0;
}
