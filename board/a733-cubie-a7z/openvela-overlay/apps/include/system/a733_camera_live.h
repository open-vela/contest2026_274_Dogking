/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APPS_INCLUDE_SYSTEM_A733_CAMERA_LIVE_H
#define __APPS_INCLUDE_SYSTEM_A733_CAMERA_LIVE_H
#include <system/a733_camera_image.h>
#include <system/a733_vision.h>
#include <errno.h>
#ifdef __cplusplus
extern "C" {
#endif

#define A733_LIVE_RGB_BYTES (640u*640u*3u)
#define A733_LIVE_MAX_SECONDS 3600u
#define A733_LIVE_MAX_FRAMES 32768u
enum a733_live_kind { A733_LIVE_PREVIEW=0,A733_LIVE_RESULT=1 };
struct a733_live_result
{
  uint32_t sequence,input_crc,npu_us,count,kind;
  struct a733_vision_message_s boxes[A733_VISION_MAX_BOXES];
};
/* Packet validation must precede ANY screen mutation. Exact frame/CRC/boxes;
 * no queue of pointers, detached inference, or stale result over a new image. */
static inline int a733_live_result_valid(const struct a733_live_result *r)
{
  if(!r || !r->sequence || r->kind>A733_LIVE_RESULT || r->count>A733_VISION_MAX_BOXES ||
     (r->kind==A733_LIVE_PREVIEW && (r->count || r->npu_us)))return -EINVAL;
  for(unsigned i=0;i<r->count;++i)
    {
      const struct a733_vision_message_s *b=&r->boxes[i];
      if(b->magic!=A733_VISION_MAGIC || b->version!=A733_VISION_VERSION ||
         b->type!=A733_VISION_DETECTION || b->sequence!=r->sequence || b->value!=r->input_crc ||
         b->count!=r->count || b->index!=i || b->score_milli>1000 ||
         b->x1>=b->x2 || b->y1>=b->y2 || b->x2>640 || b->y2>640)return -EPROTO;
    }
  return 0;
}
/* Internal flat-build application interfaces. Single worker owns LVGL/SPI;
 * synchronous copy/flush, caller buffers never retained after frame returns. */
int a733_display_live_begin(void);
int a733_display_live_frame(const uint8_t *rgb,size_t bytes,const struct a733_live_result *result);
struct a733_live_display_profile
{uint64_t crc_us,draw_us,flush_us,total_us;};
/* One atomic fresh preview OR image+result, no obligatory earlier preview. The
 * complete packet, fresh sequence and actual RGB CRC are checked together. */
int a733_display_live_present(const uint8_t *,size_t,const struct a733_live_result *,
                              struct a733_live_display_profile *);
int a733_display_live_end(void);
int a733_camera_live_busy(void);
int a733_camera_live_blocked(void); /* active OR quarantined cleanup */
/* Fresh same-frame snapshot, no pointers/queue ownership transfer. A token
 * binds one request; errors/cleanup failures NEVER publish an old result. */
int a733_camera_identify_start(const char *model,uint32_t *token);
int a733_camera_identify_poll(uint32_t token,struct a733_live_result *result);
int a733_camera_identify_stop(uint32_t token);
/* Product-owned face UI only; never stops an independent display CLI. */
int a733_display_pet_start(void);
int a733_display_pet_pause(void);
int a733_display_pet_status(void);
#ifdef CONFIG_A733_PRODUCT_V1
int a733_product_voice_reserved(void); /* foreign task must not start camera */
int a733_product_start_lock(void);
void a733_product_start_unlock(void);
int a733_product_camera_begin(void);
#endif
int a733_camera_live_command(int argc,char **argv,const struct a733_camera_tone *tone);
#ifdef __cplusplus
}
#endif
#endif
