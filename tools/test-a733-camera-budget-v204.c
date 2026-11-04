/* SPDX-License-Identifier: Apache-2.0 */
#define CONFIG_A733_CAMERA_LIVE_TEST 1
#define main previous_main
#include "test-a733-camera-mode-trial.c"
#undef main
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,660,f)==660 && fgetc(f)==EOF);fclose(f);
  fresh();prepare();assert(CAMERA_CYCLE_LIMIT==32768);
  assert(control_write(NULL,"frame-experiment",16)==16);
  for(unsigned i=2;i<=128;++i)
    {
      uint32_t previous=frame_fingerprint;frame_emitted=false;frame_pattern_bias=i;
      assert(control_write(NULL,"prepare",7)==7);
      assert(control_write(NULL,"frame-experiment",16)==16);
      assert(frame_published && repeat_ready && capture_generation==i && previous!=frame_fingerprint);
      assert(!state.controls_owned && !state.cleanup_error && !frame.owned && !receiver.owned);
    }
  struct file reader={0};assert(!frame_open(&reader));unsigned before=accesses;
  assert(control_write(NULL,"prepare",7)==-EBUSY && accesses==before);assert(!frame_close(&reader));
  /* Boundary injection is NOT 32768 real/simulated captures. */
  capture_generation=32767;frame_emitted=false;
  assert(control_write(NULL,"prepare",7)==7 && control_write(NULL,"frame-experiment",16)==16);
  before=accesses;assert(capture_generation==32768 && control_write(NULL,"prepare",7)==-EALREADY && accesses==before);
  fresh();prepare();assert(control_write(NULL,"frame-experiment",16)==16);
  frame_emitted=false;frame_bad_guard=true;assert(control_write(NULL,"prepare",7)==7);
  assert(control_write(NULL,"frame-experiment",16)==-EFAULT);before=accesses;
  assert(control_write(NULL,"prepare",7)<0 && accesses==before && !repeat_ready);
  puts("PASS v204:128 simulated changing RAW frames; budget boundary injection; reader and fault gates unchanged; NOT hardware soak");
}
