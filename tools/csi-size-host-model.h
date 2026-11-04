/* SPDX-License-Identifier: Apache-2.0 */
/* HOST ONLY. Register semantics, not image data or physical acceptance.
 * Independent of production settings: automatic reads/ignored writes model
 * the v197 board's RAW width=1920 -> BUF_LEN=0x07800780 observation; old
 * two-plane state width1280 -> 0x02800500. Other formats are NOT qualified.
 * Mode bit numbers are independently checked against the primary BSP header.
 */
#ifndef A733_HOST_CSI_SIZE_MODEL_H
#define A733_HOST_CSI_SIZE_MODEL_H
#include <stdbool.h>
#include <stdint.h>
static bool csi_size_auto_read(uint32_t a,uint32_t top,uint32_t cfg,
                               uint32_t h,uint32_t v,uint32_t *result)
{
  uint32_t width=(h>>16)&0x3fff,height=(v>>16)&0x3fff;
  if(a==0x05830238 && !(top&(1u<<29)))
    {uint32_t chroma=((cfg>>16)&15)==1?width:width/2;*result=width|(chroma<<16);return true;}
  if(a==0x0583023c && !(top&(1u<<28)))
    {*result=width|(height<<16);return true;}
  return false;
}
static bool csi_size_auto_ignores_write(uint32_t a,uint32_t top)
{return (a==0x05830238 && !(top&(1u<<29))) ||
        (a==0x0583023c && !(top&(1u<<28)));}
#endif
