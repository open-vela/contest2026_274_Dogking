/* SPDX-License-Identifier: Apache-2.0
 * Production client + protocol, actual public GATT/UUID headers, fake radio/UV.
 */
#include <assert.h>
#define main home_client_entry
#include "../app/a733home/a733home_main.c"
#undef main

enum { GOOD, NO_INSTANCE, OFF, NO_CONNECT_CB, NO_SERVICE, WRONG_PROPERTY,
       BAD_READ, NO_ACK, ACK_WRONG_SEQ, ACK_REJECTED, NOTIFY_BAD_CRC,
       WRITE_FAIL, ACK_FIRST, DISCONNECT_FAIL, MONITOR_DROP, LOOP_BUSY,
       SUBSCRIBE_FAIL, DUPLICATE_ACK, CREATE_FAIL, API_CONNECT_FAIL,
       DROP_DURING_WRITE, WRONG_NOTIFY_HANDLE };
static int mode, writes, deletes, instances, connects, drops, pump;
static uint64_t fake_ms;
static bt_instance_t instance;
static gattc_callbacks_t *callbacks;
static gattc_handle_t *user_handle;
static bt_address_t connected_peer;
static unsigned long last_sequence;

int home_test_clock_gettime(clockid_t id,struct timespec *ts)
{ (void)id; ts->tv_sec=fake_ms/1000; ts->tv_nsec=(fake_ms%1000)*1000000; return 0; }
int home_test_nanosleep(const struct timespec *req,struct timespec *rem)
{ (void)rem; fake_ms+=req->tv_sec*1000+req->tv_nsec/1000000; return 0; }
int uv_loop_init(uv_loop_t *loop) { loop->live=1; return 0; }
int uv_loop_close(uv_loop_t *loop) { if (mode==LOOP_BUSY) return -EBUSY; loop->live=0; return 0; }
int uv_run(uv_loop_t *loop,int flag)
{
  assert(loop->live && flag==UV_RUN_NOWAIT);
  pump++;
  if (mode==MONITOR_DROP && fake_ms>=30 && !drops && user_handle && *user_handle) {
    drops++;
    callbacks->on_disconnected(*user_handle,&connected_peer);
  }
  return 0;
}
bt_instance_t *bluetooth_create_instance(void)
{ if (mode==NO_INSTANCE) return NULL; instances++; instance.live=1; return &instance; }
bool bluetooth_set_external_uv(bt_instance_t *ins,uv_loop_t *loop)
{ assert(ins->live && loop->live); return true; }
void bluetooth_delete_instance(bt_instance_t *ins) { assert(ins->live); ins->live=0; instances--; }
bt_adapter_state_t bt_adapter_get_state(bt_instance_t *ins)
{ assert(ins->live); return mode==OFF ? BT_ADAPTER_STATE_OFF : BT_ADAPTER_STATE_BLE_ON; }
int bt_addr_str2ba(const char *text,bt_address_t *addr)
{ (void)text; memset(addr,0x42,sizeof(*addr)); return 0; }
bool bt_addr_is_empty(const bt_address_t *addr)
{ const bt_address_t empty={0}; return !memcmp(addr,&empty,sizeof(*addr)); }
int bt_addr_compare(const bt_address_t *a,const bt_address_t *b) { return memcmp(a,b,sizeof(*a)); }
int bt_uuid_compare(const bt_uuid_t *a,const bt_uuid_t *b) { return memcmp(a,b,sizeof(*a)); }

bt_status_t bt_gattc_create_connect(bt_instance_t *ins,gattc_handle_t *out,gattc_callbacks_t *cb)
{
  assert(ins->live && cb->size==sizeof(*cb));
  if (mode==CREATE_FAIL) return BT_STATUS_NOMEM;
  callbacks=cb; user_handle=out; *out=&instance;
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_delete_connect(gattc_handle_t handle)
{ assert(handle==&instance && user_handle && *user_handle==handle); *user_handle=NULL; deletes++; return BT_STATUS_SUCCESS; }
bt_status_t bt_gattc_connect(gattc_handle_t h,bt_address_t *addr,ble_addr_type_t type)
{
  assert(h==&instance && type==BT_LE_ADDR_TYPE_RANDOM);
  connects++; connected_peer=*addr;
  if (mode==API_CONNECT_FAIL) return BT_STATUS_FAIL;
  if (mode!=NO_CONNECT_CB) callbacks->on_connected(h,addr);
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_disconnect(gattc_handle_t h)
{
  if (mode==DISCONNECT_FAIL) return BT_STATUS_FAIL;
  callbacks->on_disconnected(h,&connected_peer);
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_discover_service(gattc_handle_t h,bt_uuid_t *filter)
{
  const uint8_t expected[]={0x01,0x10,0x77,0x4b,0x18,0x9b,0x37,0x8d,
                            0x65,0x4f,0x42,0x6b,0x01,0x00,0x33,0xa7};
  assert(filter->type==BT_UUID128_TYPE && !memcmp(filter->val.u128,expected,16));
  if (mode!=NO_SERVICE) callbacks->on_discovered(h,GATT_STATUS_SUCCESS,filter,1,20);
  callbacks->on_discovered(h,GATT_STATUS_SUCCESS,NULL,0,0);
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_get_attribute_by_uuid(gattc_handle_t h,uint16_t start,uint16_t end,
                                         bt_uuid_t *uuid,gatt_attr_desc_t *out)
{
  assert(h==&instance && start==1 && end==20);
  out->uuid=*uuid; out->type=GATT_CHARACTERISTIC;
  out->handle=uuid->val.u128[12]==2 ? 4 : 8;
  out->properties=uuid->val.u128[12]==2 ? GATT_PROP_WRITE : GATT_PROP_READ|GATT_PROP_NOTIFY;
  if (mode==WRONG_PROPERTY) out->properties=GATT_PROP_WRITE_NR;
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_subscribe(gattc_handle_t h,uint16_t attr,uint16_t ccc)
{
  assert(attr==8 && ccc==GATT_CCC_NOTIFY);
  callbacks->on_subscribed(h,mode==SUBSCRIBE_FAIL ? GATT_STATUS_FAILURE : GATT_STATUS_SUCCESS,attr,true);
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_read(gattc_handle_t h,uint16_t attr)
{
  struct home_message_s msg={.type=HOME_EVENT,.opcode=HOME_GET_STATE,.length=1};
  uint8_t bytes[HOME_MAX_FRAME];
  int len=home_encode(&msg,bytes,sizeof(bytes));
  if (mode==BAD_READ) bytes[0]^=1;
  callbacks->on_read(h,GATT_STATUS_SUCCESS,attr,bytes,len);
  return BT_STATUS_SUCCESS;
}
bt_status_t bt_gattc_write(gattc_handle_t h,uint16_t attr,uint8_t *bytes,uint16_t len)
{
  struct home_message_s request,ack={0};
  uint8_t response[HOME_MAX_FRAME];
  assert(home_decode(bytes,len,&request)==0 && attr==4);
  assert(request.type==HOME_REQUEST && (request.opcode==HOME_PING || request.opcode==HOME_GET_STATE));
  assert(request.seq!=last_sequence); last_sequence=request.seq;
  writes++;
  if (mode==WRITE_FAIL) return BT_STATUS_FAIL;
  if (mode==DROP_DURING_WRITE && writes==3 && !drops) {
    drops++; callbacks->on_disconnected(h,&connected_peer); return BT_STATUS_SUCCESS;
  }
  ack.type=HOME_ACK; ack.seq=request.seq; ack.opcode=request.opcode; ack.length=1;
  if (mode==ACK_WRONG_SEQ) ack.seq++;
  if (mode==ACK_REJECTED) ack.payload[0]=1;
  int n=home_encode(&ack,response,sizeof(response));
  if (mode==NOTIFY_BAD_CRC) response[n-1]^=1;
  uint16_t notify_handle=mode==WRONG_NOTIFY_HANDLE ? 9 : 8;
  if (mode==ACK_FIRST) callbacks->on_notified(h,notify_handle,response,n);
  callbacks->on_written(h,GATT_STATUS_SUCCESS,attr);
  if (mode!=NO_ACK && mode!=ACK_FIRST) callbacks->on_notified(h,notify_handle,response,n);
  if (mode==DUPLICATE_ACK) callbacks->on_notified(h,notify_handle,response,n);
  return BT_STATUS_SUCCESS;
}

static int run_probe(int variant)
{
  char *argv[]={"home","probe","42:42:42:42:42:42","random",NULL};
  mode=variant; writes=deletes=connects=drops=pump=0; instances=0; fake_ms=0; user_handle=NULL;
  g_needs_recovery=false; last_sequence=0;
  int result=home_client_entry(4,argv);
  assert(instances==0 && !g_client);
  assert(g_needs_recovery==(variant==DISCONNECT_FAIL || variant==LOOP_BUSY));
  if (variant!=NO_INSTANCE && variant!=OFF && variant!=CREATE_FAIL) assert(deletes==1);
  return result;
}
int main(void)
{
  for (int i=0;i<100;i++) assert(run_probe(GOOD)==0 && writes==2);
  assert(run_probe(ACK_FIRST)==0 && writes==2);
  assert(run_probe(DUPLICATE_ACK)==0 && writes==2);
  for (int variant=NO_INSTANCE;variant<=WRONG_NOTIFY_HANDLE;variant++) {
    if (variant==ACK_FIRST || variant==DUPLICATE_ACK || variant==MONITOR_DROP || variant==DROP_DURING_WRITE) continue;
    assert(run_probe(variant)!=0);
  }
  char *monitor[]={"home","monitor","42:42:42:42:42:42","random","5",NULL};
  assert(run_probe(GOOD)==0);
  mode=MONITOR_DROP; drops=0; fake_ms=0; connects=0;
  assert(home_client_entry(5,monitor)==0 && connects==2 && drops==1);
  assert(run_probe(GOOD)==0);
  mode=DROP_DURING_WRITE; drops=0; fake_ms=0; connects=0; writes=0;
  assert(home_client_entry(5,monitor)==0 && connects==2 && drops==1);
  struct home_client_s local={0};
  int before=writes;
  assert(transact(&local,HOME_SET_OUTPUT)==-EPERM && writes==before);
  char *bad[]={"home","probe","bad","random",NULL};
  assert(home_client_entry(4,bad)!=0);
  unsigned long value;
  assert(parse_number("-1",300,&value)<0 && parse_number("301",300,&value)<0);
  assert(parse_number("1x",300,&value)<0 && parse_number("0",300,&value)<0);
  assert(parse_number("2",300,&value)==0 && value==2);
  g_needs_recovery=true;
  char *valid[]={"home","probe","42:42:42:42:42:42","random",NULL};
  assert(home_client_entry(4,valid)!=0);
  g_needs_recovery=false;
  puts("home-client: 100 sessions, API errors, deadlines, CRC, stale/duplicate/early ACK, reconnect and no-actuation tests: PASS");
  return 0;
}
