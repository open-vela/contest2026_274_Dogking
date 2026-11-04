/* SPDX-License-Identifier: Apache-2.0 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main previous_main
#include "test-a733-csi-capture.c"
#undef main
#pragma GCC diagnostic pop
int main(void)
{
  fresh();struct a733_csi_capture s={0};
  assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
  for(unsigned i=0;i<64;++i)
    {
      emit=true;seen=0;assert(!a733_csi_capture_wait(&s));emit=false;
      assert(!a733_csi_capture_pause(&s) && s.owned && s.paused && !s.armed);
      assert(!values[slot(0x0582100c)] && !(values[slot(0x05830200)]&1));
      if(i!=63)assert(!a733_csi_capture_rearm(&s) && s.armed && !s.paused);
    }
  assert(!a733_csi_capture_finish(&s) && !s.owned);
  for(unsigned type=0;type<3;++type)
    {
      fresh();memset(&s,0,sizeof(s));assert(!a733_csi_capture_prepare(&s,&io,0x41000000,4*1024*1024));
      emit=true;assert(!a733_csi_capture_wait(&s));emit=false;
      if(type==0)drain_stuck=true;
      if(type==1)late_error=true;
      if(type==2)translation=true;
      assert(a733_csi_capture_pause(&s)<0 && a733_csi_capture_rearm(&s)==-EACCES);
      if(type!=1)assert(s.owned && s.cleanup_error);
      else assert(!a733_csi_capture_finish(&s) && !s.owned && s.error==-EBADMSG);
    }
  puts("PASS CSI v205: 64 pause/rearm cycles; drain, late-error and translation gates retain/quarantine safely; HOST MOCK ONLY");
  return 0;
}
