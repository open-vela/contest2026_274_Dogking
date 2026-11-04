#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute production RSN builder/M1 transaction and password editor.

Execute the production HMAC/PBKDF2/PRF over an OpenSSL SHA1 primitive and
independently verify with Python. USB is mocked; this is NOT an AP test.
"""
from pathlib import Path
import subprocess,tempfile,hmac,hashlib,os,pty,select,termios,time,signal
r=Path(__file__).resolve().parents[1];o=r/'board/a733-cubie-a7z/openvela-overlay'
s=(o/'vendor/allwinnertech/chips/a733/a733_wifi_usb.c').read_text()
def function(name,kind='int'):
    start=s.index('static '+kind+' '+name+'(');end=s.index('\n}',start)+2
    return s[start:end]
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <openssl/sha.h>
#include "wifi_secret_input.h"
#define OK 0
#define A733_WIFI_WPA_EAPOL_MAX 512
#define A733_WPA_WAIT_M1 1
#define A733_WPA_WAIT_M3 2
#define A733_WPA_COMPLETE 3
#define A733_WPA_KEY_MIC 0x0100
static struct {uint8_t mac[6],associated_bssid[6],wpa_pmk[32],wpa_ptk[48],wpa_snonce[32],wpa_anonce[32],wpa_replay[8],wpa_state;unsigned wpa_replays;} g_wifi;
static uint8_t wire[1024];static size_t wire_len;static unsigned derived;
static void a733_putbe16(uint8_t *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static int a733_wifi_data_send_ethernet(const uint8_t *p,size_t n){memcpy(wire,p,n);wire_len=n;return 0;}
typedef SHA_CTX SHA1_CTX;
#define SHA1_BLOCK_LENGTH 64
#define SHA1_DIGEST_LENGTH 20
static void sha1init(SHA1_CTX *c){assert(SHA1_Init(c));}
static void sha1update(SHA1_CTX *c,const void *p,size_t n){assert(SHA1_Update(c,p,n));}
static void sha1final(uint8_t *p,SHA1_CTX *c){assert(SHA1_Final(p,c));}
static unsigned clock_systime_ticks(void){return 42;}
'''+function('a733_wifi_hmac_sha1','void')+'\n'+function('a733_wifi_pbkdf2_block','void')+'\n'+function('a733_wifi_wpa_derive_pmk')+'\n'+function('a733_wifi_wpa_prf','void')+'\n'+function('a733_wifi_wpa_derive_ptk','void').replace('a733_wifi_wpa_derive_ptk(','test_real_derive_ptk(',1)+r'''
static void a733_wifi_wpa_derive_ptk(const uint8_t *anonce){++derived;for(unsigned i=0;i<32;i++)g_wifi.wpa_snonce[i]=anonce[i]^derived;}
'''+function('a733_wifi_wpa_send_key')+'\n'+function('a733_wifi_wpa_m1_prepare')+r'''
static void secret(const char *input,const char *expected,int status){
  char out[66]={0};size_t n=0;bool invalid=false;int rc=0;
  for(const unsigned char *p=(const unsigned char *)input;*p && !rc;p++)rc=wifi_secret_step(out,sizeof(out),&n,&invalid,*p);
  assert(rc==status);if(status==1)assert(!strcmp(out,expected));
}
int main(){
  assert(!a733_wifi_wpa_derive_pmk("password","IEEE",g_wifi.wpa_pmk));
  printf("PMK:");for(unsigned i=0;i<32;i++)printf("%02x",g_wifi.wpa_pmk[i]);puts("");
  uint8_t anonce[32];memset(anonce,0x31,sizeof(anonce));
  for(unsigned i=0;i<6;i++){g_wifi.mac[i]=i;g_wifi.associated_bssid[i]=i+6;}
  test_real_derive_ptk(anonce);
  printf("PTK:");for(unsigned i=0;i<48;i++)printf("%02x",g_wifi.wpa_ptk[i]);puts("");
  secret("1234567x\b8\n","12345678",1);
  secret("1234567x\1778\r","12345678",1);
  secret("mistaken\02512345678\n","12345678",1);
  secret(" 12345678 \n"," 12345678 ",1);
  secret("1234\033[AX5678\n","",-EINVAL);
  secret("12345678\003","",-ECANCELED);
  secret("1234567\n","",-EINVAL);
  char too_long[80];memset(too_long,'a',sizeof(too_long));too_long[78]='\n';too_long[79]=0;secret(too_long,"",-EINVAL);
  uint8_t m1[99]={0};m1[16]=1;memset(m1+17,0x31,32);g_wifi.wpa_state=A733_WPA_WAIT_M1;
  assert(!a733_wifi_wpa_m1_prepare(m1)&&derived==1);g_wifi.wpa_state=A733_WPA_WAIT_M3;
  uint8_t snonce[32];memcpy(snonce,g_wifi.wpa_snonce,32);
  assert(!a733_wifi_wpa_m1_prepare(m1)&&derived==1&&!memcmp(snonce,g_wifi.wpa_snonce,32));
  m1[16]=2;assert(!a733_wifi_wpa_m1_prepare(m1)&&derived==1);
  m1[16]=1;assert(a733_wifi_wpa_m1_prepare(m1)==-EALREADY&&g_wifi.wpa_replay[7]==2);
  m1[16]=2;g_wifi.wpa_state=A733_WPA_COMPLETE;
  assert(a733_wifi_wpa_m1_prepare(m1)==-EALREADY);
  m1[16]=3;m1[17]^=1;assert(!a733_wifi_wpa_m1_prepare(m1)&&derived==2);
  for(unsigned i=0;i<48;i++)g_wifi.wpa_ptk[i]=i;
  const uint8_t rsn[]={0x30,0x02,0x01,0x00};
  assert(!a733_wifi_wpa_send_key(0x010a,g_wifi.wpa_replay,snonce,rsn,sizeof(rsn)));
  assert(wire_len==14+99+sizeof(rsn)&&wire[14+7]==0&&wire[14+8]==0);
  printf("M2:");for(size_t i=14;i<wire_len;i++)printf("%02x",wire[i]);puts("");
  assert(!a733_wifi_wpa_send_key(0x030a,g_wifi.wpa_replay,NULL,NULL,0));
  assert(wire_len==113&&wire[21]==0&&wire[22]==0);
  printf("M4:");for(size_t i=14;i<wire_len;i++)printf("%02x",wire[i]);puts("");
  puts("PASS production password editor and RSN M2/M4 fields; nonce retransmit/replay guards. USB/derive are MOCK, NOT AP association.");
}
'''
with tempfile.TemporaryDirectory(prefix='wifi-v219-') as d:
    root=Path(d);c=root/'wire.c';c.write_text(fixture);exe=root/'wire'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror',
      '-fsanitize=address,undefined','-Wno-deprecated-declarations','-I'+str(o/'apps/system/a733wifi'),str(c),'-lcrypto','-o',str(exe)],check=True)
    run=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=20)
    print(run.stdout,end='')
    pmk=hashlib.pbkdf2_hmac('sha1',b'password',b'IEEE',4096,32)
    def prf(label,data,n):
        return b''.join(hmac.new(pmk,label+b'\0'+data+bytes([i]),hashlib.sha1).digest() for i in range((n+19)//20))[:n]
    anonce=bytes([0x31])*32;mac=bytes(range(6));ap=bytes(range(6,12))
    snonce=prf(b'A733 WPA2 SNonce',anonce+(42).to_bytes(8,'little')+mac+ap,32)
    expected_ptk=prf(b'Pairwise key expansion',mac+ap+b''.join(sorted([anonce,snonce])),48)
    for line in run.stdout.splitlines():
        if line.startswith('PMK:'):assert bytes.fromhex(line[4:])==pmk
        if line.startswith('PTK:'):assert bytes.fromhex(line[4:])==expected_ptk
        if not line.startswith(('M2:','M4:')):continue
        packet=bytearray.fromhex(line.split(':')[1]);mic=bytes(packet[81:97]);packet[81:97]=bytes(16)
        assert mic==hmac.new(bytes(range(16)),packet,hashlib.sha1).digest()[:16]
        assert int.from_bytes(packet[2:4],'big')+4==len(packet)
    print('PASS independent Python PMK, PTK and EAPOL HMAC/lengths (public test key only; NOT RF acceptance).')
    # Execute the ACTUAL nested-command terminal reader, not just its editor.
    cli=(o/'apps/system/a733wifi/a733wifi_main.c').read_text()
    def cli_function(declaration):
        start=cli.index(declaration);return cli[start:cli.index('\n}',start)+2]
    terminal=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include "wifi_secret_input.h"
#define WIFI_PASSWORD_MAX 66
static volatile sig_atomic_t g_cancel;
static void interrupt_input(int signo){(void)signo;g_cancel=1;}
'''+cli_function('static void wifi_clear(')+'\n'+cli_function('static int wifi_password(')+r'''
int main(int argc,char **argv){
  assert(argc==2);struct sigaction action={0};action.sa_handler=interrupt_input;
  sigemptyset(&action.sa_mask);assert(!sigaction(SIGINT,&action,NULL));
  char password[66]={0};int rc=wifi_password(password);assert(rc==atoi(argv[1]));
  if(!rc)assert(!strcmp(password,"12345678"));
  else for(unsigned i=0;i<sizeof(password);i++)assert(!password[i]);
  puts("PASS real terminal reader: no echo, edited bytes, restored terminal, failure wipes secret");return 0;
}
'''
    c=root/'terminal.c';c.write_text(terminal);exe=root/'terminal'
    subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror',
      '-fsanitize=address,undefined','-I'+str(o/'apps/system/a733wifi'),str(c),'-o',str(exe)],check=True)
    for value,expected in [(b'1234567x\x7f8\n',0),(b'a'*78+b'\n',-22),(None,-4)]:
        master,slave=pty.openpty();original=termios.tcgetattr(slave)
        child=subprocess.Popen([str(exe),str(expected)],stdin=slave,stdout=slave,stderr=subprocess.PIPE)
        data=bytearray();deadline=time.monotonic()+10
        while b'WPA2 passphrase: ' not in data and time.monotonic()<deadline:
            if select.select([master],[],[],0.5)[0]:data.extend(os.read(master,4096))
        assert b'WPA2 passphrase: ' in data
        assert not termios.tcgetattr(slave)[3]&(termios.ECHO|termios.ICANON)
        if value is None:child.send_signal(signal.SIGINT)
        else:os.write(master,value)
        code=child.wait(timeout=10);error=child.stderr.read().decode();assert code==0,(code,error)
        assert termios.tcgetattr(slave)==original
        os.close(slave)
        while select.select([master],[],[],0.1)[0]:
            try:data.extend(os.read(master,4096))
            except OSError:break
        os.close(master);assert b'1234567' not in data
        print('PASS production PTY password input:',expected,'terminal restored; synthetic input not echoed')
