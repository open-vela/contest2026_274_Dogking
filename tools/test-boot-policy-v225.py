#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real CRC records + real startup dispatcher with explicitly mocked spawn."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];sdk=(r/'../../quickly-openvela').resolve()
ov=r/'board/a733-cubie-a7z/openvela-overlay'
source=r'''
#include <assert.h>
#include <spawn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
static unsigned pets=0,wifi=0,agent=0,opens=0,console=0;
static int mock_open(posix_spawn_file_actions_t *a,int fd,const char *name,int flags,mode_t mode){
  (void)a;(void)mode;assert(fd>=0&&fd<=2);assert(flags==(fd?O_WRONLY:O_RDONLY));
  if(!fd)assert(!strcmp(name,"/dev/null"));
  if(!strcmp(name,"/dev/a7zconsole"))++console;
  else assert(!strcmp(name,"/dev/null"));
  ++opens;return 0;
}
static int mock_spawn(pid_t *pid,const char *name,const posix_spawn_file_actions_t *a,
 const posix_spawnattr_t *attr,char *const argv[],char *const env[]){
 (void)a;(void)attr;(void)env;assert(opens==3&&argv[1]&&!argv[2]);opens=0;*pid=100;
 if(!strcmp(name,"aipet")&&!strcmp(argv[1],"--pet-boot")){++pets;assert(console==2);}
 else {assert(console==0);if(!strcmp(name,"wifi"))++wifi;if(!strcmp(name,"aipet"))++agent;}
 console=0;return 0;
}
#define posix_spawn mock_spawn
#define posix_spawn_file_actions_addopen mock_open
#define main dispatcher_main
#include "@SERVICES@/a733services_main.c"
#undef main
#undef posix_spawn
#undef posix_spawn_file_actions_addopen
int main(void){
 char *args[]={"service","--boot",NULL};
 assert(!a733_auto_enabled("pet"));assert(a733_auto_enabled("wifi"));
 assert(!dispatcher_main(2,args)&&pets==0&&wifi==1&&agent==1);
 unsigned char wifi_record[]={1,2,3,4},back[4]={0};
 assert(!a733_config_write("wifi.profile",wifi_record,4));
 assert(!a733_auto_option("pet","on")&&a733_auto_enabled("pet"));
 assert(!dispatcher_main(2,args)&&pets==1&&wifi==2&&agent==2);
 assert(!a733_config_read("wifi.profile",back,4)&&!memcmp(back,wifi_record,4));
 assert(!a733_auto_option("pet","off")&&!a733_auto_enabled("pet"));
 assert(!dispatcher_main(2,args)&&pets==1);
 assert(!a733_auto_option("pet","on"));
 FILE *f=fopen(A733_CONFIG_DIR "/pet.auto","r+b");assert(f);assert(!fseek(f,12,SEEK_SET));
 assert(fputc(2,f)==2);assert(!fclose(f));
 assert(!a733_auto_enabled("pet"));assert(!dispatcher_main(2,args)&&pets==1);
 assert(a733_auto_option("../pet","on")<0);
 puts("PASS v225 BOOT POLICY: real persisted CRC opt-in/off/corrupt-off, existing Wi-Fi record preserved; real dispatcher starts pet only with opt-in, console output and /dev/null input. Spawn is mocked, NOT reboot acceptance.");
}
'''
with tempfile.TemporaryDirectory(prefix='petboot225-') as d:
    tmp=Path(d);(tmp/'nuttx').mkdir();(tmp/'nuttx/config.h').write_text('')
    src=tmp/'test.c';src.write_text(source.replace('@SERVICES@',str(ov/'apps/system/a733services')))
    exe=tmp/'policy'
    subprocess.run(['gcc','-D_DEFAULT_SOURCE','-DCONFIG_SYSTEM_A733STORAGE=1',
      '-DCONFIG_EXAMPLES_AI_AGENT_VELA=1','-DCONFIG_A733_PRODUCT_V1=1',
      '-DA733_CONFIG_DIR="'+str(tmp/'state')+'"','-DA733_CONFIG_MOUNT="'+str(tmp)+'"',
      '-O1','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function',
      '-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-pthread',
      '-I'+str(tmp),'-I'+str(ov/'apps/include'),
      '-I'+str(sdk/'apps/crypto/mbedtls/mbedtls/include'),str(src),
      str(ov/'apps/system/a733services/services_config.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
