/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_UFS_VOLUME_H
#define A733_UFS_VOLUME_H
#include <errno.h>
#include <stdint.h>
#include "a733_ufs_wire.h"
/* Existing FAT32 only. Never repair, format, or reinterpret an ext4 volume.
 * Sector count is the fixed exported view, NOT the larger GPT extent. */
static inline unsigned ufs_v16(const uint8_t *p)
{ return (unsigned)p[0]|(unsigned)p[1]<<8; }
static inline int ufs_volume_boot(const uint8_t *p,uint32_t blocks,
                                unsigned *info,unsigned *backup)
{
  if(!p||!info||!backup)return -EINVAL;
  unsigned reserved=ufs_v16(p+14),spc=p[13],fats=p[16];
  uint32_t size=ufs_getle32(p+36),root=ufs_getle32(p+44);
  uint64_t metadata=(uint64_t)reserved+(uint64_t)fats*size;
  *info=ufs_v16(p+48);*backup=ufs_v16(p+50);
  if(p[510]!=0x55||p[511]!=0xaa||ufs_v16(p+11)!=4096||!spc||
     spc>128||(spc&(spc-1))||reserved<8||reserved>4096||fats!=2||
     ufs_v16(p+17)||ufs_v16(p+19)||ufs_v16(p+22)||
     ufs_getle32(p+32)!=blocks||!size||metadata>=blocks||
     !*info||*info>=reserved||!*backup||*backup>=reserved||*info==*backup||
     ufs_v16(p+42)||ufs_v16(p+40))return -EINVAL;
  uint64_t clusters=(blocks-metadata)/spc;
  if(clusters<65525||clusters>=0x0ffffff5||root<2||root>=clusters+2||
     (uint64_t)size*1024<clusters+2)return -EINVAL;
  return 0;
}
static inline int ufs_volume_info(const uint8_t *p)
{
  return p&&ufs_getle32(p)==0x41615252&&ufs_getle32(p+484)==0x61417272&&
    ufs_getle32(p+508)==0xaa550000?0:-EINVAL;
}
#endif
