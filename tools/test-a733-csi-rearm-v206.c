/* SPDX-License-Identifier: Apache-2.0 */
/* Conservative consumed-address host model, NOT a hardware simulator. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main old_main
#include "test-a733-csi-capture.c"
#undef main
#pragma GCC diagnostic pop
static unsigned submitted;
static bool address_ready;
static int queue_write(void *p,uint32_t a,uint32_t v,unsigned ms)
{
  if(a==0x05830200 && !(v&1)){submitted=0;address_ready=false;}
  if(a==0x05830220 || a==0x05830228 || a==0x05830230)
    {
      /* Submission must occur while DMA is stopped. Same value is a NEW
       * write, not interchangeable with observing the previous register. */
      assert(!(values[slot(0x05830200)]&1));
      submitted|=1u<<((a-0x05830220)/8);
    }
  if(a==0x05830200 && (v&1))address_ready=submitted==7;
  return wr(p,a,v,ms);
}
static int queue_read(void *p,uint32_t a,uint32_t *v,unsigned ms)
{
  int r=rd(p,a,v,ms);
  if(!r && a==0x05830254 && emit && !address_ready)*v|=0x00220083;
  return r;
}
static const struct a733_imx214_cold_io queue_io={
  .read32=queue_read,.write32=queue_write,.now_ms=now,.delay_ms=delay};
static void setup(struct a733_csi_capture *s)
{
  fresh();submitted=0;address_ready=false;memset(s,0,sizeof(*s));
  assert(!a733_csi_capture_prepare(s,&queue_io,0x41000000,4*1024*1024));
  emit=true;assert(!a733_csi_capture_wait(s));emit=false;
  assert(!a733_csi_capture_pause(s));
}
int main(int argc,char **argv)
{
  bool legacy=argc==2 && !strcmp(argv[1],"legacy");
  struct a733_csi_capture s;setup(&s);
  unsigned before=calls;assert(!a733_csi_capture_rearm(&s));
  unsigned rearm_calls=calls-before;
  if(legacy)
    {
      assert(!address_ready);emit=true;
      assert(a733_csi_capture_wait(&s)==-EBADMSG && s.status==0x00220083);
      emit=false;assert(!a733_csi_capture_finish(&s) && !s.owned);
      puts("PASS archived v205 reproduces second-frame 00220083 with a consumed address latch; HOST HYPOTHESIS ONLY");return 0;
    }
  for(unsigned i=0;i<64;++i)
    {
      assert(address_ready && submitted==7);emit=true;seen=0;
      assert(!a733_csi_capture_wait(&s));emit=false;
      assert(!a733_csi_capture_pause(&s));
      if(i!=63)assert(!a733_csi_capture_rearm(&s));
    }
  assert(!a733_csi_capture_finish(&s) && !s.owned);
  for(unsigned at=1;at<=rearm_calls;++at)
    {
      setup(&s);fail_at=calls+at;
      assert(a733_csi_capture_rearm(&s)<0 && failed);
      assert(!s.ready && s.error<0);
      if(submitted!=7)assert(!(values[slot(0x05830200)]&1));
      int c=a733_csi_capture_finish(&s);
      assert(!c && !s.owned);
    }
  setup(&s);assert(!a733_csi_capture_rearm(&s));
  values[slot(0x05830254)]=0x00220083;emit=true;
  assert(a733_csi_capture_wait(&s)==-EBADMSG);emit=false;
  assert(s.first_fault.address==0x05830254 && s.first_fault.mask==0x002f8044 && s.first_fault.observed==0x00220083);
  assert(!a733_csi_capture_finish(&s) && !s.cleanup_error && !s.owned);
  printf("PASS v206:64 re-submitted frames, %u callback-after-effect rearm failures, exact board status preserved; HOST ONLY\n",rearm_calls);
  return 0;
}
