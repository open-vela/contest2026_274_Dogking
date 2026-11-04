#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute actual optional CRC + ID/affinity gate in freestanding QEMU ARM."""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
sdk=(r/'../../quickly-openvela').resolve()
cc=sdk/'prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-gcc'
objdump=cc.with_name('aarch64-none-elf-objdump')
qemu=sdk/'prebuilts/qemu/linux-x86_64/bin/qemu-system-aarch64'
header=r/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_tensor_crc.inc'
with tempfile.TemporaryDirectory(prefix='tensor210-arm-') as raw:
    d=Path(raw)
    (d/'sched.h').write_text('typedef unsigned long cpu_set_t; extern cpu_set_t test_mask; static inline int sched_getaffinity(int p,unsigned long n,cpu_set_t*m){(void)p;(void)n;*m=test_mask;return 0;}\n')
    (d/'test.c').write_text('#include "'+str(header)+'"\n'+r"""
unsigned long test_mask=1;
static unsigned char data[2048016];
void *memcpy(void*d,const void*s,unsigned long n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
void *memset(void*d,int c,unsigned long n){unsigned char*a=d;while(n--)*a++=(unsigned char)c;return d;}
static int check(unsigned off,unsigned n,unsigned seed)
{
  unsigned a=99,b=99,c=99;
  unsigned x=a733_raw_arm_scan(data+off,n,seed,&a);
  unsigned y=a733_raw_software_scan(data+off,n,seed,&b);
  unsigned z=a733_raw_scan(data+off,n,seed,&c);
  unsigned tensor=a733_tensor_crc32part(data+off,n,seed);
  return tensor!=y || x!=y || y!=z || a!=b || b!=c;
}
int test(void)
{
  test_mask=3;if(a733_raw_hw_ready())return 1; /* unpinned */
  test_mask=2;if(a733_raw_hw_ready())return 2; /* wrong CPU */
  test_mask=1;if(!a733_raw_hw_ready())return 3; /* MRS + actual 640-case selftest */
  for(unsigned v=0;v<256;++v)
    {
      for(unsigned i=0;i<256;++i)data[i]=(unsigned char)v;
      for(unsigned off=0;off<8;++off)for(unsigned n=0;n<80;++n)
        if(check(off,n,0x19ba0055u^n))return 4;
    }
  unsigned state=208;
  for(unsigned i=0;i<sizeof(data);++i){state=state*1664525u+1013904223u;data[i]=(unsigned char)(state>>24);}
  const unsigned sizes[]={1228800,1638400,2048000,409600,512000,102400,128000};
  for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);++i)
    {
      if(check(i,sizes[i],0x12345678u))return 5;
      unsigned split=sizes[i]/3,seed=0x89abcdef;
      unsigned partial=a733_raw_arm_scan(data+i,split,seed,0);
      partial=a733_raw_arm_scan(data+i+split,sizes[i]-split,partial,0);
      if(partial!=a733_raw_software_scan(data+i,sizes[i],seed,0))return 6;
    }
  return 0;
}
""")
    (d/'start.S').write_text('.section .text.start\n.global _start\n_start:\nldr x0, =stack_top\nmov sp, x0\nldr x0, =vectors\nmsr vbar_el1, x0\nisb\nbl test\nexit_test:\nadr x1, exit_block\nstr x0, [x1, #8]\nmov x0, #0x20\nhlt #0xf000\nb .\nfault:\nmov x0, #99\nb exit_test\n.balign 2048\nvectors:\n.rept 16\nb fault\n.space 124\n.endr\n.align 3\nexit_block: .quad 0x20026, 99\n.bss\n.align 12\n.space 65536\nstack_top:\n')
    (d/'link.ld').write_text('ENTRY(_start) SECTIONS { . = 0x40080000; .text : { *(.text.start) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=d/'test.elf'
    # MMU-off harness has Device memory attributes; byte-safe loads avoid
    # its unaligned Device fault. Production keeps normal cached RAM flags.
    subprocess.run([str(cc),'-std=c11','-march=armv8-a','-mstrict-align','-mgeneral-regs-only','-mno-outline-atomics','-O2','-fno-builtin','-ffreestanding','-nostdlib',
      '-D__NuttX__','-DCONFIG_A733_CAMERA_PERF_TEST=1','-I'+str(d),
      str(d/'start.S'),str(d/'test.c'),'-T',str(d/'link.ld'),'-o',str(elf)],check=True)
    asm=subprocess.check_output([str(objdump),'-d',str(elf)],text=True)
    assert 'crc32x' in asm and 'crc32b' in asm and 'crc32cx' not in asm
    lib=r/'out/qemu-deps-v208/root/usr/lib/x86_64-linux-gnu'
    env={**os.environ}
    if lib.exists():env['LD_LIBRARY_PATH']=str(lib)+':'+str(lib/'pulseaudio')
    p=subprocess.run([str(qemu),'-M','virt','-cpu','max','-nodefaults','-nic','none','-nographic','-monitor','none',
       '-serial','none','-semihosting','-kernel',str(elf)],capture_output=True,text=True,timeout=60,env=env)
    assert not p.returncode,p.stdout+p.stderr+str(p.returncode)
print('PASS actual TENSOR wrapper + ARM IEEE CRC opcodes, MRS/affinity gate, on-core selftest, seeds/alignment/chunks/counts/all six output sizes under QEMU; NOT A733 timing')
