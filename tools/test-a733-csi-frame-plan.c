/* SPDX-License-Identifier: Apache-2.0 */
#include "a733_csi_frame_plan.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
  struct a733_csi_frame_plan p;
  assert(!a733_csi_raw10_frame_plan(1920,1080,0x10000000,5000000,&p));
  assert(p.stride==3840 && p.allocation_rows==1088);
  assert(p.active_bytes==4147200 && p.payload_capacity==4177920);
  assert(p.allocation_bytes==4178048 && p.payload_iova==0x10000040);
  assert(p.address_word==0x04000010 && p.prefix_guard==64 && p.suffix_guard==64);
  uint32_t size=p.allocation_bytes;
  uint64_t last=(UINT64_C(1)<<34)-size;
  assert(!a733_csi_raw10_frame_plan(1920,1080,last,size,&p));
  struct a733_csi_frame_plan saved=p;
  assert(a733_csi_raw10_frame_plan(1920,1080,last+64,size,&p)==-ERANGE);
  assert(!memcmp(&p,&saved,sizeof(p)));
  assert(a733_csi_raw10_frame_plan(1920,1080,UINT64_MAX-63,size,&p)==-ERANGE);
  assert(a733_csi_raw10_frame_plan(1920,1080,0x10000001,size,&p)==-EINVAL);
  assert(a733_csi_raw10_frame_plan(1920,1080,0x10000000,size-1,&p)==-EMSGSIZE);
  assert(a733_csi_raw10_frame_plan(640,640,0x10000000,size,&p)==-EINVAL);
  assert(a733_csi_raw10_frame_plan(1920,1080,0x10000000,size,NULL)==-EINVAL);
  puts("PASS: CSI candidate RAW10/LE16 stride/rows/guards, 34-bit address encoding/aperture/overflow; NO DMA or mapping executed");
  return 0;
}
