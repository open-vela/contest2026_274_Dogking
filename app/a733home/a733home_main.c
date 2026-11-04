/* SPDX-License-Identifier: Apache-2.0
 * Product client over official openvela GAP/GATT/IPC; no private ATT/HCI.
 * All GATT callbacks run on our external UV loop, never the IPC worker pool.
 * No blocking API is invoked from a callback. SET_OUTPUT is NOT exposed.
 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <bluetooth.h>
#include <bt_adapter.h>
#include <bt_gattc.h>
#include "home_protocol.h"

#ifndef HOME_STEP_TIMEOUT_MS
#define HOME_STEP_TIMEOUT_MS 15000
#endif
#define HOME_POLL_MS 10
#define HOME_DISCONNECT_TIMEOUT_MS 5000

enum home_wait_e { WAIT_CONNECT, WAIT_DISCOVER, WAIT_SUBSCRIBE, WAIT_READ,
                   WAIT_ACK, WAIT_DISCONNECT };
struct home_client_s {
  uv_loop_t loop;
  bt_instance_t *instance;
  gattc_handle_t gatt; /* SDK retains this pointer until delete_connect. */
  bt_address_t peer;
  uint16_t start, end, command, status, seq;
  uint8_t opcode;
  bool connected, disconnected, discovered, subscribed, read, written, ack;
  bool pending;
  int error;
  unsigned int notifications, invalid, stale, acks;
};
static struct home_client_s *g_client;
static pthread_mutex_t g_run_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t g_stop;
static uint16_t g_sequence;
static bool g_needs_recovery;

static uint64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void stop_requested(int signo)
{
  (void)signo;
  g_stop = 1;
}

static bt_uuid_t profile_uuid(uint8_t id)
{
  bt_uuid_t uuid = BT_UUID_DECLARE_128(0x01,0x10,0x77,0x4b,0x18,0x9b,0x37,0x8d,
                                     0x65,0x4f,0x42,0x6b,0x01,0x00,0x33,0xa7);
  uuid.val.u128[12] = id;
  return uuid;
}

static struct home_client_s *client_for(gattc_handle_t handle)
{
  return g_client && handle == g_client->gatt ? g_client : NULL;
}

static void on_connected(gattc_handle_t handle, bt_address_t *addr)
{
  struct home_client_s *c = client_for(handle);
  if (!c) return;
  if (!addr || bt_addr_compare(addr, &c->peer)) { c->error=-EPROTO; return; }
  c->connected=true;
  c->disconnected=false;
  puts("home: connected callback");
}

static void on_disconnected(gattc_handle_t handle, bt_address_t *addr)
{
  struct home_client_s *c = client_for(handle);
  (void)addr;
  if (!c) return;
  c->connected=false;
  c->disconnected=true;
  c->pending=false;
  puts("home: disconnected callback; no action replay");
}

static void on_discovered(gattc_handle_t handle, gatt_status_t status,
                          bt_uuid_t *uuid, uint16_t start, uint16_t end)
{
  struct home_client_s *c = client_for(handle);
  bt_uuid_t wanted=profile_uuid(1);
  if (!c) return;
  if (status != GATT_STATUS_SUCCESS) { c->error=-EPROTO; return; }
  if (!uuid || !uuid->type) { c->discovered=true; return; }
  if (!bt_uuid_compare(uuid, &wanted)) {
    if (!start || start > end || c->start) { c->error=-EPROTO; return; }
    c->start=start;
    c->end=end;
    printf("home: service %04x..%04x\n", start, end);
  }
}

static void on_read(gattc_handle_t handle, gatt_status_t status,
                    uint16_t attr, uint8_t *value, uint16_t length)
{
  struct home_client_s *c = client_for(handle);
  struct home_message_s msg;
  if (!c || attr != c->status) return;
  if (status != GATT_STATUS_SUCCESS || home_decode(value,length,&msg) < 0 ||
      msg.type == HOME_REQUEST || msg.opcode != HOME_GET_STATE ||
      (msg.type == HOME_ACK && msg.payload[0] != 0)) {
    c->error=-EPROTO;
    return;
  }
  c->read=true;
  printf("home: status read valid type=%u seq=%u bytes=%u\n",
         msg.type, msg.seq, msg.length);
}

static void on_written(gattc_handle_t handle, gatt_status_t status, uint16_t attr)
{
  struct home_client_s *c = client_for(handle);
  if (!c || !c->pending || attr != c->command) return;
  if (status != GATT_STATUS_SUCCESS) { c->error=-EIO; return; }
  c->written=true;
  puts("home: ATT write response (application ACK still required)");
}

static void on_subscribed(gattc_handle_t handle, gatt_status_t status,
                          uint16_t attr, bool enable)
{
  struct home_client_s *c = client_for(handle);
  if (!c || attr != c->status) return;
  if (status != GATT_STATUS_SUCCESS || !enable) { c->error=-EIO; return; }
  c->subscribed=true;
  puts("home: notification subscription complete");
}

static void on_notified(gattc_handle_t handle, uint16_t attr,
                        uint8_t *value, uint16_t length)
{
  struct home_client_s *c = client_for(handle);
  struct home_message_s msg;
  if (!c || attr != c->status) return;
  c->notifications++;
  if (home_decode(value,length,&msg) < 0 || msg.type == HOME_REQUEST) {
    c->invalid++;
    puts("home: invalid notification rejected");
    return;
  }
  if (msg.type == HOME_EVENT) {
    printf("home: event opcode=%u bytes=%u\n", msg.opcode,msg.length);
    return;
  }
  if (!c->pending || c->ack || msg.seq != c->seq || msg.opcode != c->opcode) {
    c->stale++;
    return;
  }
  if (msg.payload[0] != 0) { c->error=-EREMOTEIO; return; }
  c->ack=true;
  c->acks++;
  printf("home: business ACK seq=%u opcode=%u status=0\n",msg.seq,msg.opcode);
}

static gattc_callbacks_t g_callbacks = {
  .size=sizeof(gattc_callbacks_t), .on_connected=on_connected,
  .on_disconnected=on_disconnected, .on_discovered=on_discovered,
  .on_read=on_read, .on_written=on_written, .on_subscribed=on_subscribed,
  .on_notified=on_notified
};

static int wait_for(struct home_client_s *c, enum home_wait_e wait,
                    unsigned int timeout)
{
  uint64_t until=now_ms()+timeout;
  const struct timespec pause={0,HOME_POLL_MS*1000000};
  for (;;) {
    bool done=false;
    uv_run(&c->loop, UV_RUN_NOWAIT);
    if (wait != WAIT_DISCONNECT) {
      if (g_stop) return -EINTR;
      if (c->error) return c->error;
      if (c->disconnected) return -ENOTCONN;
    }
    switch (wait) {
      case WAIT_CONNECT: done=c->connected; break;
      case WAIT_DISCOVER: done=c->discovered; break;
      case WAIT_SUBSCRIBE: done=c->subscribed; break;
      case WAIT_READ: done=c->read; break;
      case WAIT_ACK: done=c->written && c->ack; break;
      case WAIT_DISCONNECT: done=c->disconnected; break;
    }
    if (done) return 0;
    if (now_ms() >= until) return -ETIMEDOUT;
    nanosleep(&pause,NULL);
  }
}

static int api_result(const char *stage, bt_status_t status)
{
  printf("home: %s API=%d\n",stage,status);
  return status == BT_STATUS_SUCCESS ? 0 : -EIO;
}

static int find_characteristic(struct home_client_s *c, uint8_t id,
                                uint32_t required, uint16_t *out)
{
  bt_uuid_t uuid=profile_uuid(id);
  gatt_attr_desc_t attr={0};
  int rc=api_result("attribute",bt_gattc_get_attribute_by_uuid(
                    c->gatt,c->start,c->end,&uuid,&attr));
  if (rc < 0) return rc;
  if (attr.type != GATT_CHARACTERISTIC || !attr.handle ||
      attr.handle < c->start || attr.handle > c->end ||
      (attr.properties & required) != required || bt_uuid_compare(&uuid,&attr.uuid))
    return -EPROTO;
  *out=attr.handle;
  printf("home: characteristic id=%u handle=%04x properties=%" PRIx32 "\n",
         id,attr.handle,attr.properties);
  return 0;
}

static int transact(struct home_client_s *c, uint8_t opcode)
{
  uint8_t bytes[HOME_MAX_FRAME];
  struct home_message_s msg={0};
  int n, rc;
  if (opcode != HOME_PING && opcode != HOME_GET_STATE) return -EPERM;
  if (++g_sequence == 0) ++g_sequence;
  c->seq=g_sequence; c->opcode=opcode;
  c->ack=c->written=false; c->pending=true;
  msg.type=HOME_REQUEST; msg.seq=c->seq; msg.opcode=opcode;
  n=home_encode(&msg,bytes,sizeof(bytes));
  if (n < 0) { c->pending=false; return n; }
  printf("home: write request seq=%u opcode=%u bytes=%d\n",c->seq,opcode,n);
  rc=api_result("write",bt_gattc_write(c->gatt,c->command,bytes,n));
  if (!rc) rc=wait_for(c,WAIT_ACK,HOME_STEP_TIMEOUT_MS);
  c->pending=false;
  return rc;
}

static int open_profile(struct home_client_s *c, ble_addr_type_t addr_type)
{
  bt_uuid_t uuid=profile_uuid(1);
  int rc;
  c->connected=c->disconnected=c->discovered=c->subscribed=c->read=false;
  c->error=0; c->start=c->end=c->command=c->status=0;
  puts("home: connect begin");
  rc=api_result("connect",bt_gattc_connect(c->gatt,&c->peer,addr_type));
  if (!rc) rc=wait_for(c,WAIT_CONNECT,HOME_STEP_TIMEOUT_MS);
  if (rc) return rc;
  puts("home: discovery begin");
  rc=api_result("discover",bt_gattc_discover_service(c->gatt,&uuid));
  if (!rc) rc=wait_for(c,WAIT_DISCOVER,HOME_STEP_TIMEOUT_MS);
  if (rc) return rc;
  if (!c->start) return -ENOENT;
  rc=find_characteristic(c,2,GATT_PROP_WRITE,&c->command);
  if (!rc) rc=find_characteristic(c,3,GATT_PROP_READ|GATT_PROP_NOTIFY,&c->status);
  if (rc) return rc;
  rc=api_result("subscribe",bt_gattc_subscribe(c->gatt,c->status,GATT_CCC_NOTIFY));
  if (!rc) rc=wait_for(c,WAIT_SUBSCRIBE,HOME_STEP_TIMEOUT_MS);
  if (rc) return rc;
  rc=api_result("read",bt_gattc_read(c->gatt,c->status));
  if (!rc) rc=wait_for(c,WAIT_READ,HOME_STEP_TIMEOUT_MS);
  if (!rc) rc=transact(c,HOME_PING);
  if (!rc) rc=transact(c,HOME_GET_STATE);
  return rc;
}

static int disconnect_peer(struct home_client_s *c)
{
  int rc;
  if (c->disconnected) return 0;
  puts("home: disconnect/cancel begin");
  rc=api_result("disconnect",bt_gattc_disconnect(c->gatt));
  if (!rc) rc=wait_for(c,WAIT_DISCONNECT,HOME_DISCONNECT_TIMEOUT_MS);
  return rc;
}

static int parse_number(const char *text, unsigned long maximum, unsigned long *out)
{
  char *end;
  unsigned long val;
  if (!text || !*text || *text=='-' || *text=='+') return -EINVAL;
  errno=0; val=strtoul(text,&end,10);
  if (errno || *end || !val || val>maximum) return -EINVAL;
  *out=val;
  return 0;
}

static void usage(void)
{
  puts("home info\n"
       "home frame <ping|state|ack-ping|ack-state|event-state> [seq=1]\n"
       "home probe <AA:BB:CC:DD:EE:FF> <public|random>\n"
       "home monitor <address> <public|random> <seconds:1..300>\n"
       "Start kvdbd/bluetoothd and enable BLE with bttool first; stop scan and quit.\n"
       "No auto pairing, SET_OUTPUT, physical actuation or Agent invocation.");
}

static int offline_frame(int argc, char **argv)
{
  struct home_message_s msg={0};
  uint8_t bytes[HOME_MAX_FRAME];
  unsigned long seq=1;
  int n;
  if (argc<3 || argc>4 || (argc==4 && parse_number(argv[3],65535,&seq))) return 1;
  msg.type=HOME_REQUEST; msg.seq=seq;
  if (!strcmp(argv[2],"ping")) msg.opcode=HOME_PING;
  else if (!strcmp(argv[2],"state")) msg.opcode=HOME_GET_STATE;
  else if (!strcmp(argv[2],"ack-ping")) { msg.type=HOME_ACK; msg.opcode=HOME_PING; msg.length=1; }
  else if (!strcmp(argv[2],"ack-state")) { msg.type=HOME_ACK; msg.opcode=HOME_GET_STATE; msg.length=1; }
  else if (!strcmp(argv[2],"event-state")) { msg.type=HOME_EVENT; msg.opcode=HOME_GET_STATE; msg.length=1; }
  else return 1;
  n=home_encode(&msg,bytes,sizeof(bytes));
  if (n<0) return 1;
  for (int i=0;i<n;i++) printf("%s%02x",i?" ":"",bytes[i]);
  puts("");
  return 0;
}

int main(int argc, char **argv)
{
  /* Retain UV storage even if cleanup fails; refuse reuse until reboot. */
  static struct home_client_s client;
  bt_address_t peer;
  ble_addr_type_t type;
  unsigned long seconds=0;
  unsigned int reconnects=0;
  struct sigaction action={0}, previous;
  bool signal_saved=false;
  int rc=0, close_rc=0;
  if (argc==2 && !strcmp(argv[1],"info")) {
    printf("openvela official GATT client v145; no physical output\nservice=%s\ncommand=%s\nstatus=%s\nneeds-recovery=%u\n",
           HOME_SERVICE_UUID,HOME_COMMAND_UUID,HOME_STATUS_UUID,g_needs_recovery);
    return 0;
  }
  if (argc>=2 && !strcmp(argv[1],"frame")) return offline_frame(argc,argv);
  if (argc<4 || argc>5 || (strcmp(argv[1],"probe") && strcmp(argv[1],"monitor")) ||
      (!strcmp(argv[1],"probe") && argc!=4) ||
      (!strcmp(argv[1],"monitor") && (argc!=5 || parse_number(argv[4],300,&seconds)))) {
    usage(); return 1;
  }
  if (strlen(argv[2])!=17) { puts("home: invalid address"); return 1; }
  for (int i=0;i<17;i++) {
    if ((i%3==2 && argv[2][i]!=':') ||
        (i%3!=2 && !isxdigit((unsigned char)argv[2][i]))) return 1;
  }
  if (bt_addr_str2ba(argv[2],&peer)<0 || bt_addr_is_empty(&peer)) return 1;
  if (!strcmp(argv[3],"public")) type=BT_LE_ADDR_TYPE_PUBLIC;
  else if (!strcmp(argv[3],"random")) type=BT_LE_ADDR_TYPE_RANDOM;
  else { puts("home: address type must be public or random"); return 1; }
  if (pthread_mutex_trylock(&g_run_lock)) { puts("home: another client is active"); return 1; }
  if (g_needs_recovery) {
    puts("home: previous cleanup failed; preserve logs and reboot before retry");
    pthread_mutex_unlock(&g_run_lock);
    return 1;
  }
  memset(&client,0,sizeof(client));
  client.peer=peer;
  g_client=&client; g_stop=0;
  if (uv_loop_init(&client.loop)) { rc=-ENOMEM; goto end; }
  action.sa_handler=stop_requested; sigemptyset(&action.sa_mask);
  signal_saved=sigaction(SIGINT,&action,&previous)==0;
  if (!signal_saved) { rc=-EIO; goto loop_end; }
  puts("home: create official Bluetooth instance begin");
  client.instance=bluetooth_create_instance();
  if (!client.instance) { puts("home: no instance; check ps, start kvdbd/bluetoothd"); rc=-ENOTCONN; goto loop_end; }
  if (!bluetooth_set_external_uv(client.instance,&client.loop)) { rc=-EIO; goto loop_end; }
  bt_adapter_state_t state=bt_adapter_get_state(client.instance);
  if (state!=BT_ADAPTER_STATE_BLE_ON && state!=BT_ADAPTER_STATE_ON) {
    printf("home: adapter state=%d; enable with bttool first\n",state); rc=-ENETDOWN; goto loop_end;
  }
  rc=api_result("create",bt_gattc_create_connect(client.instance,&client.gatt,&g_callbacks));
  if (rc) goto loop_end;
  rc=open_profile(&client,type);
  if (seconds && (!rc || (rc==-ENOTCONN && client.disconnected))) {
    uint64_t until=now_ms()+seconds*1000;
    rc=0;
    while (!g_stop && now_ms()<until) {
      uint64_t next=now_ms()+2000;
      while (!g_stop && !client.disconnected && now_ms()<next && now_ms()<until) {
        const struct timespec pause={0,HOME_POLL_MS*1000000};
        uv_run(&client.loop,UV_RUN_NOWAIT); nanosleep(&pause,NULL);
      }
      if (g_stop || now_ms()>=until) break;
      if (client.disconnected) {
        if (++reconnects>3) { rc=-ECONNRESET; break; }
        printf("home: reconnect %u/3 same peer; fresh discovery, no action replay\n",reconnects);
        rc=open_profile(&client,type);
      } else rc=transact(&client,HOME_GET_STATE);
      if (rc==-ENOTCONN && client.disconnected) { rc=0; continue; }
      if (rc) break;
    }
  }
  if (g_stop && !rc) rc=-EINTR;
  if (client.gatt) close_rc=disconnect_peer(&client);
loop_end:
  /* External-loop callbacks cannot race deletion: this owner thread does not
   * pump them during these calls. SDK deinit closes and drains its IPC input.
   */
  if (client.gatt) {
    int status=bt_gattc_delete_connect(client.gatt);
    if (status!=BT_STATUS_SUCCESS && !close_rc) close_rc=-EIO;
  }
  if (client.instance) bluetooth_delete_instance(client.instance);
  uv_run(&client.loop,UV_RUN_NOWAIT); /* Drain uv_close callbacks, not GATT work. */
  if (uv_loop_close(&client.loop) && !close_rc) close_rc=-EBUSY;
  if (signal_saved) sigaction(SIGINT,&previous,NULL);
end:
  if (close_rc) {
    g_needs_recovery=true;
    puts("home: cleanup incomplete; preserve logs and reboot before retry");
  }
  if (!rc) rc=close_rc;
  printf("home: result=%d cleanup=%d ACKs=%u notify=%u invalid=%u stale=%u reconnects=%u\n",
         rc,close_rc,client.acks,client.notifications,client.invalid,client.stale,reconnects);
  g_client=NULL;
  pthread_mutex_unlock(&g_run_lock);
  return rc ? 1 : 0;
}
