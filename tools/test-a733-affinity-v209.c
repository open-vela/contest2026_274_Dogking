/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <system/a733_cpu_qualify.h>
struct fake
{
  uint32_t mask;unsigned cpu,part,waits;uint64_t tick;
  int never,stop,fail_get,fail_set,fail_restore,wrong_readback,unstable,stalled;
};
static int get(void *v,uint32_t *out)
{struct fake *f=v;if(f->fail_get)return -ESRCH;*out=f->wrong_readback?255:f->mask;return 0;}
static int set(void *v,uint32_t mask)
{struct fake *f=v;if(f->fail_set || (mask==255 && f->fail_restore))return -EINVAL;f->mask=mask;return 0;}
static int id(void *v,uint64_t *midr,uint64_t *mpidr)
{
  struct fake *f=v;*midr=(uint64_t)(f->part?f->part:(f->cpu<6?0xd05:0xd0b))<<4;
  *mpidr=(uint64_t)f->cpu<<8;return f->unstable?-EAGAIN:0;
}
static uint64_t tick(void *v){return ((struct fake *)v)->tick;}
static void wait(void *v)
{
  struct fake *f=v;f->waits++;if(!f->stalled)f->tick+=1000;
  /* Model the real kernel's deferred affinity: only a blocked yield moves
   * execution; a successful set(mask) alone leaves it on CPU1. */
  if(!f->never)f->cpu=f->mask==64?6:7;
}
static int stop(void *v){return ((struct fake *)v)->stop;}
int main(void)
{
  const struct a733_cpu_ops op={get,set,id,tick,wait,stop};
  struct fake f={.mask=255,.cpu=1};struct a733_cpu_proof p;
  assert(!a733_cpu_qualify(&op,&f,6,0xd0b,&p));
  assert(f.cpu==6 && f.mask==64 && p.confirmations==2 && f.waits==2 && p.attempts==3 && p.elapsed==2000);
  assert(!a733_cpu_qualify(&op,&f,7,0xd0b,&p) && f.cpu==7 && p.before==64 && p.readback==128);
  for(unsigned mode=0;mode<9;mode++)
    {
      f=(struct fake){.mask=255,.cpu=1,.never=1};
      if(mode==1)f.stop=1;if(mode==2)f.fail_get=1;if(mode==3)f.fail_set=1;
      if(mode==4)f.fail_restore=1;if(mode==5)f.wrong_readback=1;
      if(mode==6)f.unstable=1;if(mode==7){f.part=0xd05;f.never=0;}if(mode==8)f.stalled=1;
      assert(a733_cpu_qualify(&op,&f,6,0xd0b,&p)<0);
      assert(p.attempts<=251 && f.waits<=251);
      assert(mode==4 ? p.restore_error==-EINVAL : !p.restore_error);
      assert(f.mask==255 || mode==4);
      if(mode==8)assert(p.elapsed==0 && p.error==-ETIMEDOUT);
    }
  f=(struct fake){.mask=255};assert(a733_cpu_qualify(&op,&f,8,0xd0b,&p)==-EINVAL);
  puts("PASS actual migration protocol: deferred switch, stable double proof, bounded timeout, stop, API/identity failure and exact restore; MOCK CPU ONLY");
}
