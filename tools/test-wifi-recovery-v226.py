#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'board/a733-cubie-a7z/openvela-overlay/apps/system/a733wifi/a733wifi_main.c').read_text()
start=s.index('static bool wifi_recovery_healthy(');end=s.index('\n}',start)+2
fixture='#include <stdbool.h>\n#include <string.h>\n#include <assert.h>\n'+s[start:end]+r'''
int main(){
 assert(wifi_recovery_healthy("wpa2: checkpoint=0 port=1 dhcp: checkpoint=0 address=10.0.0.2"));
 assert(!wifi_recovery_healthy("wpa2: checkpoint=0 port=1 dhcp: checkpoint=0 address=0.0.0.0"));
 assert(!wifi_recovery_healthy("wpa2: checkpoint=0 port=0 dhcp: checkpoint=0 address=10.0.0.2"));
 assert(!wifi_recovery_healthy("wpa2: checkpoint=-100 port=0 dhcp: checkpoint=0 address=10.0.0.2"));
 assert(!wifi_recovery_healthy("wpa2: checkpoint=0 port=1 dhcp: checkpoint=-11 address=10.0.0.2"));
 assert(!wifi_recovery_healthy(0));
}'''
with tempfile.TemporaryDirectory(prefix='wifi-recovery-v226-') as d:
 p=Path(d);(p/'test.c').write_text(fixture)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
assert 'if (healthy) puts("wifi-recovery-v226: healthy' in s
assert 'saved-profile single attempt' in s and '!strcmp(argv[1], "--recover")' in s
runtime=(r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/product_runtime.cxx').read_text()
assert '(p->ifa_flags&IFF_RUNNING)' in runtime and 'if(product_network())return 0;' in runtime
assert 'waitpid(child,&status,WNOHANG)' in runtime and 'kill(child,SIGINT)' in runtime and 'SIGKILL' not in runtime
assert 'posix_spawnattr_setsigmask(&attributes,&unblocked)' in runtime
assert 'posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETSIGMASK)' in runtime
assert 'posix_spawn(&child,program,&actions,&attributes,args,nullptr)' in runtime
begin=runtime.index('  posix_spawnattr_t attributes;')
end=runtime.index('  pid_t child=0;',begin)
actual=runtime[begin:end]
fixture='#include <signal.h>\n#include <spawn.h>\n#include <sys/wait.h>\n#include <unistd.h>\n#include <assert.h>\nextern char **environ;\nint main(){int e=0;sigset_t blocked,saved;sigemptyset(&blocked);sigaddset(&blocked,SIGINT);assert(!sigprocmask(SIG_BLOCK,&blocked,&saved));\n'+actual+r'''
 assert(!e);
 pid_t child;char program[]="/bin/sh",opt[]="-c",command[]="kill -INT $$; exit 99";char *args[]={program,opt,command,0};
 assert(!posix_spawn(&child,program,0,&attributes,args,environ));
 int status;assert(waitpid(child,&status,0)==child);
 assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGINT);
 posix_spawnattr_destroy(&attributes);assert(!sigprocmask(SIG_SETMASK,&saved,0));
}'''
with tempfile.TemporaryDirectory(prefix='wifi-child-mask-v226-') as d:
 p=Path(d);(p/'test.c').write_text(fixture)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
print("PASS v226 actual Wi-Fi health predicate: carrier+IPv4 at product boundary, WPA2/port/DHCP before skip; serialized saved-profile recovery, no healthy reconnect or reset. HOST, not RF/AP acceptance.")
print("PASS actual production spawn attributes: child receives SIGINT despite blocked parent; own child only, no hardware reset.")
