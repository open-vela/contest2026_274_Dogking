/* SPDX-License-Identifier: Apache-2.0 */
#define main old_mode_main
#include "test-a733-camera-mode-trial.c"
#undef main
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,660,f)==660 && fgetc(f)==EOF);assert(!fclose(f));
  assert((uintptr_t)frame_buffer>=0x41000000 && (uintptr_t)frame_buffer<0x47000000);
  fresh();char byte;struct file fp={0};assert(frame_read(&fp,&byte,1)==-ENODATA);
  assert(control_write(NULL,"frame-experiment",16)==-EACCES && !accesses);
  fresh();prepare();assert(control_write(NULL,"frame-experiment",16)==16);
  assert(frame_published && frame.completed && !frame.error && !frame.cleanup_error && !frame.owned);
  assert(!receiver.owned && !state.controls_owned && !sensor[0x100] && frame_min==0 && frame_max==1023);
  assert(frame_read(&fp,&byte,1)==1 && fp.f_pos==1);
  fp.f_pos=frame.plan.active_bytes;assert(frame_read(&fp,&byte,1)==0);
  assert(frame_read(&fp,NULL,1)==-EINVAL);
  struct file report={0};assert(!control_open(&report));
  assert(strstr(report.f_priv,"frame-final:") && strstr(report.f_priv,"normal boot has no handoff"));
  assert(!control_close(&report));
  assert(control_write(NULL,"frame-experiment",16)<0);
  fresh();prepare();frame_bad_guard=true;
  assert(control_write(NULL,"frame-experiment",16)==-EFAULT && !frame_published && !frame.owned);
  fresh();prepare();frame_bad_pixel=true;
  assert(control_write(NULL,"frame-experiment",16)==-EPROTO && !frame_published && !frame.owned);
  fresh();prepare();frame_late_error=true;
  assert(control_write(NULL,"frame-experiment",16)==-EBADMSG && !frame_published && !frame.owned && !frame.cleanup_error);
  assert(frame.first_fault.address==0x05830254);
  puts("PASS: actual frame/control/TWI/receiver/SHA composition, guarded 1080p content, stable post-stop memory, no publication on guard/format failure; SIMULATED DMA ONLY");
}
