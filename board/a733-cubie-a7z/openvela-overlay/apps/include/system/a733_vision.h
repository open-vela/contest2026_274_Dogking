/****************************************************************************
 * apps/include/system/a733_vision.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_INCLUDE_SYSTEM_A733_VISION_H
#define __APPS_INCLUDE_SYSTEM_A733_VISION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define A733_VISION_QUEUE_NAME "/a733_vision"
#define A733_VISION_MAGIC      UINT32_C(0x56373341) /* A73V */
#define A733_VISION_VERSION    2
#define A733_VISION_MAX_BOXES  7
#define A733_VISION_MODEL_SIZE 640

enum a733_vision_message_type_e
{
  A733_VISION_FRAME = 1,
  A733_VISION_DETECTION = 2
};

/* Keep this structure at CONFIG_MQ_MAXMSGSIZE (32 bytes).  Coordinates are
 * whole pixels in the fixed 640x640 model space; the display owner performs
 * the final mapping. FRAME value is inference latency; x1/y1 carry the low/
 * high halves of the NuttX seed-zero input CRC32, x2/y2 are model dimensions.
 * DETECTION value also carries input CRC. This accidental-mismatch binding is
 * NOT authentication/cryptographic proof.
 */

struct a733_vision_message_s
{
  uint32_t magic;
  uint32_t sequence;
  uint16_t version;
  uint16_t type;
  uint16_t index;
  uint16_t count;
  uint16_t class_id;
  uint16_t score_milli;
  uint16_t x1;
  uint16_t y1;
  uint16_t x2;
  uint16_t y2;
  uint32_t value;
};

/* Exact NuttX crc32part convention: seed zero, no pre/post XOR. A zero
 * image has CRC zero; do NOT substitute the usual zlib default convention. */
#include <system/a733_crc32_fast.h>
static inline uint32_t a733_vision_crc32(const uint8_t *p,size_t n,uint32_t crc)
{return a733_raw_crc32(p,n,crc);}
struct a733_vision_batch_s
{
  struct a733_vision_message_s boxes[A733_VISION_MAX_BOXES];
  uint32_t sequence,required_crc,input_crc,received;
  uint16_t count;
  bool require_crc,seen,accepting,complete;
};
enum a733_vision_update_e
{ A733_VISION_INVALID=-1,A733_VISION_IGNORED=0,A733_VISION_NEW_FRAME=1,
  A733_VISION_COMPLETE=2,A733_VISION_WRONG_INPUT=3 };
static inline int a733_vision_batch_feed(struct a733_vision_batch_s *s,
                                         const struct a733_vision_message_s *m)
{
  if(m->magic!=A733_VISION_MAGIC || m->version!=A733_VISION_VERSION || !m->sequence)
    return A733_VISION_INVALID;
  if(m->type==A733_VISION_FRAME)
    {
      if(m->count>A733_VISION_MAX_BOXES || m->x2!=640 || m->y2!=640)return A733_VISION_INVALID;
      if(s->seen && (int32_t)(m->sequence-s->sequence)<=0)return A733_VISION_IGNORED;
      s->seen=true;s->sequence=m->sequence;s->received=0;s->count=m->count;
      uint32_t crc=(uint32_t)m->x1|((uint32_t)m->y1<<16);
      s->input_crc=crc;
      s->accepting=!s->require_crc || crc==s->required_crc;s->complete=false;
      if(!s->accepting)return A733_VISION_WRONG_INPUT;
      if(!s->count){s->complete=true;return A733_VISION_COMPLETE;}
      return A733_VISION_NEW_FRAME;
    }
  if(m->type!=A733_VISION_DETECTION)return A733_VISION_INVALID;
  if(!s->accepting || s->complete || m->sequence!=s->sequence)return A733_VISION_IGNORED;
  if(m->value!=s->input_crc || m->count!=s->count || m->index>=s->count || m->score_milli>1000 ||
     m->x1>=m->x2 || m->y1>=m->y2 || m->x2>640 || m->y2>640)
    {s->accepting=false;return A733_VISION_INVALID;}
  uint32_t bit=UINT32_C(1)<<m->index;
  if(s->received&bit)return A733_VISION_IGNORED;
  s->boxes[m->index]=*m;s->received|=bit;
  if(s->received==((UINT32_C(1)<<s->count)-1))
    {s->complete=true;return A733_VISION_COMPLETE;}
  return A733_VISION_IGNORED; /* partial batches never appear as complete */
}

#endif
