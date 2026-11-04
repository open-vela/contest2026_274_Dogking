/* SPDX-License-Identifier: Apache-2.0 */
/* Extracted production functions; mocks do not simulate USB/controller radio. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define BLE_LIFE_LOG(...) ((void)0)
#define BT_LOGE(...) ((void)0)
#define BT_LOGI(...) ((void)0)
#define K_NO_WAIT 0
#define K_MSEC(ms) (ms)
struct k_work { int busy; };
struct net_buf { int refs, type; unsigned len; uint8_t *data; };
struct queue { struct net_buf *buf; };
struct bt_dev_scan_ctx;
struct bt_dev {
    int dev_id;
    struct bt_dev_scan_ctx *scan_ctx;
    struct k_work init, tx_work, rx_work;
    struct queue cmd_tx_queue, rx_queue;
    struct net_buf *sent_cmd;
};
static struct bt_dev *current;
static bool permanently_busy;
static unsigned sleeps, released;
static int k_work_cancel(struct k_work *work) { return work->busy; }
static int k_work_busy_get(struct k_work *work) { return work->busy; }
static void k_sleep(int ms)
{
    assert(ms == 2);
    if (++sleeps == 3 && !permanently_busy)
        current->init.busy = current->tx_work.busy = current->rx_work.busy = 0;
}
static struct net_buf *take_buf(struct queue *queue)
{
    struct net_buf *buf = queue->buf;
    queue->buf = NULL;
    return buf;
}
static struct net_buf *k_fifo_get(struct queue *queue, int timeout)
{ assert(timeout == K_NO_WAIT); return take_buf(queue); }
static struct net_buf *net_buf_slist_get(struct queue *queue) { return take_buf(queue); }
static void net_buf_unref(struct net_buf *buf) { assert(buf->refs == 1); buf->refs--; released++; }
#include "quiesce.h"

struct k_mutex { pthread_mutex_t native; };
struct bt_le_scan_param { int type, interval, window; };
struct bt_dev_scan_ctx {
    struct bt_dev *hdev;
    bool mutexes_initialized;
    int scan_cbs;
    struct {
        unsigned long scan_flags[1];
        struct bt_le_scan_param explicit_scan_param, used_scan_param;
        struct k_mutex scan_update_mutex, scan_explicit_params_mutex;
    } scan_state;
} scan_ctxs[1];
static unsigned mutex_inits;
static void sys_slist_init(int *list) { *list = 0; }
static void k_mutex_init(struct k_mutex *mutex)
{ assert(pthread_mutex_init(&mutex->native, NULL) == 0); mutex_inits++; }
static void bt_scan_softreset(struct bt_dev *dev) { assert(dev->scan_ctx); }
#include "scan-reset.h"

static int write_mode, writes, delays;
static int mock_write(int fd, uint8_t *buf, int count)
{
    assert(fd == 1 && buf && count > 0);
    writes++;
    if (write_mode == 1) { errno = EAGAIN; return -1; }
    if (write_mode == 2) return 0;
    if (write_mode == 3) { errno = EBADF; return -1; }
    if (writes == 1) { errno = EAGAIN; return -1; }
    return count > 3 ? 3 : count;
}
static void mock_usleep(int us) { assert(us == 1000); delays++; }
#define write mock_write
#define usleep mock_usleep
#include "h4-write.h"
#undef write
#undef usleep

struct h4_data { int fd; };
struct device { void *data; };
static struct h4_data h4 = {1};
static const struct device device = {&h4};
enum { BT_BUF_ACL_OUT, BT_BUF_CMD, BT_BUF_ISO_OUT };
enum { BT_HCI_H4_ACL=2, BT_HCI_H4_CMD=1, BT_HCI_H4_ISO=5 };
#define IS_ENABLED(x) 0
static int bt_buf_get_type(struct net_buf *buf) { return buf->type; }
static void net_buf_push_u8(struct net_buf *buf, uint8_t byte) { *--buf->data=byte; buf->len++; }
static uint8_t net_buf_pull_u8(struct net_buf *buf) { buf->len--; return *buf->data++; }
static void h4_data_dump(const char *tag, int type, uint8_t *data, unsigned len)
{ (void)tag; (void)type; (void)data; (void)len; }
#include "h4-send.h"

typedef int bt_controller_id_t;
typedef int bt_status_t;
enum { BT_STATUS_NOMEM = 1, BT_STATUS_SUCCESS = 0 };
#define STACK_CALL(name) name
#define SAL_CHECK(rc,expected) assert((rc)==(expected))
typedef struct {
    bt_controller_id_t id;
    void (*func)(void *);
    struct bt_le_scan_param params;
} sal_scan_req_t;
static struct bt_le_scan_param scan_param, received;
static sal_scan_req_t *pending;
static void zblue_on_device_found(void) {}
static int bt_le_scan_start(const struct bt_le_scan_param *p, void (*cb)(void))
{ assert(cb == zblue_on_device_found); received = *p; return 0; }
#include "sal-start.h"
static sal_scan_req_t *sal_scan_req(int id, void (*func)(void *))
{ sal_scan_req_t *req = calloc(1, sizeof(*req)); assert(req); req->id=id; req->func=func; return req; }
static int sal_send_req(sal_scan_req_t *req) { assert(!pending); pending=req; return 0; }
#include "sal-enqueue.h"

int main(void)
{
    struct bt_dev dev = {0};
    current = &dev;
    for (unsigned i=0; i<100; i++) {
        struct net_buf cmd={.refs=1}, rx={.refs=1}, sent={.refs=1};
        sleeps=0; released=0;
        dev.init.busy=dev.tx_work.busy=dev.rx_work.busy=1;
        dev.cmd_tx_queue.buf=&cmd; dev.rx_queue.buf=&rx; dev.sent_cmd=&sent;
        assert(bt_dev_quiesce(&dev)==0);
        assert(sleeps==3 && released==3 && !dev.sent_cmd);
        bt_scan_reset(&dev);
        assert(mutex_inits==2);
        assert(pthread_mutex_lock(&dev.scan_ctx->scan_state.scan_update_mutex.native)==0);
        assert(pthread_mutex_unlock(&dev.scan_ctx->scan_state.scan_update_mutex.native)==0);
        dev.scan_ctx->scan_state.scan_flags[0]=7;
    }
    permanently_busy=true; dev.rx_work.busy=1; sleeps=0; released=0;
    assert(bt_dev_quiesce(&dev)==-ETIMEDOUT);
    assert(sleeps==1000 && released==0 && dev.rx_work.busy==1);
    assert(pthread_mutex_destroy(&scan_ctxs[0].scan_state.scan_update_mutex.native)==0);
    assert(pthread_mutex_destroy(&scan_ctxs[0].scan_state.scan_explicit_params_mutex.native)==0);
    uint8_t data[11]={0};
    writes=delays=0; assert(h4_send_data(1,data,sizeof(data))==11 && delays==1);
    write_mode=1; writes=delays=0; assert(h4_send_data(1,data,11)==-ETIMEDOUT && writes==2000);
    write_mode=2; assert(h4_send_data(1,data,11)==-EIO);
    write_mode=3; assert(h4_send_data(1,data,11)==-1);
    for (int mode=0; mode<4; mode++) {
        uint8_t storage[12]={0};
        struct net_buf buf={.refs=1, .type=BT_BUF_CMD, .len=11, .data=storage+1};
        write_mode=mode; writes=delays=0; released=0;
        int rc=h4_send(&device,&buf);
        if (mode==0) {
            assert(rc==0 && released==1 && buf.refs==0);
        } else {
            assert(rc<0 && released==0 && buf.refs==1 && buf.len==11 && buf.data==storage+1);
            net_buf_unref(&buf); /* Host's error cleanup owns the sole reference. */
            assert(released==1 && buf.refs==0);
        }
    }
    scan_param.interval=160; scan_param.window=80;
    assert(bt_sal_le_start_scan(0)==0);
    scan_param.interval=999;
    pending->func(pending); free(pending); pending=NULL;
    assert(received.interval==160 && received.window==80);
    puts("Zblue reopen: 100 quiesce/mutex resets, bounded timeout/write, H4 error ownership, queued parameter snapshot: PASS");
    return 0;
}
