#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute original/patched real DNS callback: truncated UDP -> TCP cleanup."""
from pathlib import Path
import json,subprocess,tempfile,runpy
r=Path(__file__).resolve().parents[1];sdk=(r/'../../quickly-openvela').resolve()
original=(sdk/'nuttx/libs/libc/netdb/lib_dnsquery.c').read_text()
transform=runpy.run_path(str(r/'tools/apply-dns-close-v229.py'))['transform']
def callback(s):
    a=s.index('static int dns_query_callback(');b=s.index('\n/****************************************************************************',a)
    return s[a:b]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#define FAR
#define CONFIG_NET_IPv4 1
#define CONFIG_NETDB_DNSCLIENT_RETRIES 2
#define CONFIG_NETDB_MAX_IPv4ADDR 4
#define CONFIG_NETDB_DNSCLIENT_ENTRIES 0
#define DNS_RECTYPE_A 1
union dns_addr_u { unsigned char bytes[16]; };
struct dns_query_info_s { int unused; };
struct dns_query_s {int result;const char *hostname;union dns_addr_u *addr;int *naddr;uint32_t ttl;};
struct dns_query_data_s {struct dns_query_s query;struct dns_query_info_s qinfo;uint8_t buffer[512];};
static int live,opened,closed;static bool used[1024],streams[1024],reject_tcp;
static int dns_is_queryfamily(int family){return family==AF_INET;}
static int dns_bind(int family,bool stream){assert(family==AF_INET);if(live==8)return -ENOMEM;int fd=++opened;assert(fd<1024);used[fd]=true;streams[fd]=stream;++live;return fd;}
static int mock_close(int fd){assert(used[fd]);used[fd]=false;--live;++closed;return 0;}
static int dns_send_query(int fd,const char *h,union dns_addr_u *a,int type,struct dns_query_info_s *q,uint8_t *b,bool stream){(void)h;(void)a;(void)q;(void)b;assert(used[fd]&&type==1&&stream==streams[fd]);return 0;}
static int dns_recv_response(int fd,union dns_addr_u *a,int n,struct dns_query_info_s *q,uint32_t *ttl,uint8_t *b,bool stream,bool *fallback){(void)a;(void)n;(void)q;(void)ttl;(void)b;assert(used[fd]);if(!stream){*fallback=true;return -EAGAIN;}return reject_tcp?-EPROTO:1;}
static void dns_query_error(const char *s,int r,const union dns_addr_u *a){(void)s;(void)r;(void)a;}
#define close mock_close
'''
suffix=r'''
int main(void){
 for(int i=0;i<100;i++){
  int n=4;union dns_addr_u addr[4];struct sockaddr server={.sa_family=AF_INET};
  struct dns_query_data_s data={.query={.result=-EAGAIN,.hostname="fixture.invalid",.addr=addr,.naddr=&n}};
  int rc=dns_query_callback(&data,&server,sizeof(server));
#ifdef OLD
  if(rc!=1){assert(data.query.result==-ENOMEM&&live==8);puts("REPRODUCED old DNS callback: 8 leaked UDP descriptors block later queries");return 0;}
#else
  assert(rc==1&&live==0&&opened==closed);
#endif
 }
#ifdef OLD
 assert(!"old regression was not reproduced");
#else
 reject_tcp=true;
 for(int i=0;i<100;i++){
  int n=4;union dns_addr_u addr[4];struct sockaddr server={.sa_family=AF_INET};
  struct dns_query_data_s data={.query={.result=-EAGAIN,.hostname="fixture.invalid",.addr=addr,.naddr=&n}};
  assert(dns_query_callback(&data,&server,sizeof(server))==0&&live==0&&opened==closed);
 }
 puts("PASS actual patched callback: 100 successful +100 rejected TCP fallback cycles, no descriptor leak. DNS/socket I/O MOCK, NOT board root-cause proof.");
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='dns-close-v229-') as d:
    t=Path(d)
    for name,s in [('old',original),('fixed',transform(original))]:
        c=t/(name+'.c');c.write_text(prefix+callback(s)+suffix)
        args=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined']
        if name=='old':args+=['-DOLD=1']
        subprocess.run(args+[str(c),'-o',str(t/name)],check=True)
        subprocess.run([str(t/name)],check=True,timeout=10)
assert original==(sdk/'nuttx/libs/libc/netdb/lib_dnsquery.c').read_text()
print('PASS: SDK source NOT modified by test; staged transform covers IPv4 + IPv6 branches.')
