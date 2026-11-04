#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host function-body contracts; no hardware/firmware/boot acceptance."""
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SDK = (REPO/'../../quickly-openvela').resolve()


def body(source, name):
    start = re.search(r'^(?:static )?(?:int|void) '+name+r'\([^;]*?\)\n\{',
                      source, re.M).start()
    brace = source.index('{', start)
    depth = 1
    end = brace+1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def run():
    patch = (REPO/'patches/nuttx-a733-boot-progress-v188.patch').read_bytes()
    files = re.findall(r'^--- a/(.*)$', patch.decode(), re.M)
    with tempfile.TemporaryDirectory(prefix='a733-boot188-') as folder:
        temp = Path(folder)
        for name in files:
            target = temp/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SDK/name, target)
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', folder],
                       input=patch, check=True, stdout=subprocess.DEVNULL)
        cpu = (temp/'nuttx/arch/arm64/src/common/arm64_cpustart.c').read_text()
        smp = (temp/'nuttx/sched/init/nx_smpstart.c').read_text()
        ctor = (temp/'nuttx/libs/libc/misc/lib_cxx_initialize.c').read_text()
        common = '''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_A733_BOOT_DIAGNOSTICS 1
#define DEBUGASSERT assert
#define serr(...) ((void)0)
#define sinfo(...) ((void)0)
#define OK 0
static unsigned markers;
void a733_boot_value(const char *stage, uint64_t value) { markers++; }
'''
        cases = {}
        cases['psci_smp_error_propagation'] = common + '''
#define CONFIG_ARM64_PSCI 1
#define CONFIG_NCPUS 8
#define CONFIG_SMP_NCPUS 8
static int calls, failcpu, flushes;
int this_cpu(void) { return 0; }
uint64_t arm64_get_mpid(int cpu) { return (uint64_t)cpu << 8; }
void __start(void) { }
int psci_cpu_on(uint64_t mpid, uint64_t entry) {
  assert(entry == (uint64_t)__start);
  assert(mpid == (uint64_t)(++calls) << 8);
  return calls == failcpu ? -16 : 0;
}
void up_flush_dcache_all(void) { flushes++; }
''' + body(cpu, 'arm64_start_cpu') + '\n' + body(cpu, 'up_cpu_start') + \
            '\n' + body(smp, 'nx_smp_start') + '''
int main(void) {
  assert(nx_smp_start() == 0 && calls == 7 && flushes == 1);
  assert(markers == 16);
  calls=markers=0; failcpu=3;
  assert(nx_smp_start() == -16 && calls == 3 && flushes == 2);
  assert(markers == 8);
  return 0;
}
'''
        cases['constructor_order_and_null_entry'] = common + '''
#define CONFIG_HAVE_CXXINITIALIZE 1
typedef void (*initializer_t)(void);
static int sequence;
static void first(void) { assert(sequence++ == 0); }
static void second(void) { assert(sequence++ == 1); }
static initializer_t fixtures[] = {first, NULL, second};
#define _sinit fixtures
#define _einit (fixtures+3)
''' + body(ctor, 'lib_cxx_initialize') + '''
int main(void) {
  lib_cxx_initialize();
  assert(sequence == 2 && markers == 4);
  return 0;
}
'''
        for name, source in cases.items():
            path = temp/(name+'.c')
            path.write_text(source)
            executable = temp/name
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            str(path), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
        wrapper = (REPO/'tools/build-a733-wapi.sh').read_text()
        for file in files + ['nuttx/sched/init/nx_start.c',
                             'nuttx/sched/init/nx_bringup.c']:
            assert '\n  '+file+'\n' in wrapper, 'SDK backup missing: '+file
        assert 'CONFIG_BOARD_INITTHREAD_STACKSIZE=262144' in wrapper
        assert 'CONFIG_NSH_ARCHINIT=y' in wrapper
        return dict(result='HOST_CONTRACTS_PASSED_NOT_BOOT_PROOF',
                    cases=list(cases), patch_fuzz=0, sanitizers=True,
                    sdk_modified=False, hardware_executed=False)


if __name__ == '__main__':
    print(json.dumps(run(), indent=2))
