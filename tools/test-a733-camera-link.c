/* SPDX-License-Identifier: Apache-2.0 */
/* Actual node, SHA, mode, TWI and receiver; fake MMIO/sensor only. */
#define main old_mode_main
#include "test-a733-camera-mode-trial.c"
#undef main
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,660,f)==660 && fgetc(f)==EOF);assert(!fclose(f));
  fresh();assert(control_write(NULL,"link-experiment",15)==-EACCES && !accesses);
  fresh();prepare();assert(control_write(NULL,"link-experiment",15)==15);
  assert(link_attempted && state.mode_trial.verified_registers==14 && receiver.raw_headers);
  assert(receiver.fs && receiver.fe && !receiver.errors && !sensor[0x100]);
  assert(sensor_writes==155 && !stream_error && !stop_error);
  assert(!receiver.owned && !receiver.ready && !receiver.cleanup_error);restored();
  unsigned before=accesses;assert(control_write(NULL,"link-experiment",15)==-EACCES && accesses==before);
  struct a733_imx214_twi3 denied={0};
  assert(a733_imx214_twi3_packet_stream(&denied,NULL,NULL,true)<0 && accesses==before);
  puts("PASS: actual control/SHA/mode/TWI/receiver packet composition with moving calibration/counter fields, ON/OFF/readback/restore, one-shot and default deny; FAKE HARDWARE");
  return 0;
}
