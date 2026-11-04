/* SPDX-License-Identifier: Apache-2.0 */
#define main previous_main
#include "test-a733-camera-mode-trial.c"
#undef main
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,660,f)==660 && fgetc(f)==EOF);fclose(f);
  fresh();struct file reader={0};assert(frame_open(&reader)==-ENODATA);
  prepare();assert(control_write(NULL,"frame-experiment",16)==16);
  assert(frame_published && repeat_ready && capture_generation==1);
  assert(sensor_lines==2128 && sensor_exposure==2118 && sensor_gain==0);
  assert(!g_a733_camera_power_handoff.ready && !g_a733_camera_mode_handoff.ready);
  assert(!frame_open(&reader) && frame_readers==1);
  unsigned before=accesses;assert(control_write(NULL,"prepare",7)==-EBUSY && accesses==before);
  char sample;assert(frame_read(&reader,&sample,1)==1);assert(!frame_close(&reader) && !frame_readers);
  uint32_t previous=frame_fingerprint;
  for(unsigned i=2;i<=32;++i)
    {
      frame_emitted=false;frame_pattern_bias=i;
      assert(control_write(NULL,"prepare",7)==7 && !frame_published);
      assert(control_write(NULL,"frame-experiment",16)==16);
      assert(frame_published && repeat_ready && capture_generation==i);
      assert(frame_fingerprint!=previous);previous=frame_fingerprint;
      assert(!frame.owned && !receiver.owned && !state.controls_owned && !state.cleanup_error);
    }
  before=accesses;assert(control_write(NULL,"prepare",7)==-EALREADY && accesses==before);
  fresh();prepare();assert(control_write(NULL,"frame-experiment",16)==16);
  frame_emitted=false;frame_bad_guard=true;
  assert(control_write(NULL,"prepare",7)==7);
  assert(control_write(NULL,"frame-experiment",16)==-EFAULT && !frame_published && !repeat_ready);
  before=accesses;assert(control_write(NULL,"prepare",7)<0 && accesses==before);
  fresh();prepare();assert(control_write(NULL,"frame-experiment",16)==16);
  struct file readers[16]={{0}};
  for(unsigned i=0;i<16;++i)assert(!frame_open(readers+i));
  assert(frame_open(&reader)==-EMFILE);
  for(unsigned i=0;i<16;++i)assert(!frame_close(readers+i));
  frame.owned=true;before=accesses;
  assert(control_write(NULL,"prepare",7)==-EIO && accesses==before);
  assert(control_write(NULL,"park",4)==-EBUSY && accesses==before);
  frame.owned=false;assert(control_write(NULL,"prepare",7)==7);
  mode_bytes[32]^=1;before=accesses;
  assert(control_write(NULL,"frame-experiment",16)==-EBADMSG && accesses==before);
  puts("PASS v203: 32 changing frames; cached SHA; reader leases; no failed/owned DMA rearm; no PMIC retry; SIMULATED ONLY");
}
