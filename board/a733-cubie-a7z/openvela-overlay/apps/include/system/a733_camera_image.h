/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APPS_INCLUDE_SYSTEM_A733_CAMERA_IMAGE_H
#define __APPS_INCLUDE_SYSTEM_A733_CAMERA_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* CPU image preparation only. No sensor, CSI, ISP, DMA or NPU access.
 * RAW10_LE16 means right-justified 10 bits in little-endian 16-bit words;
 * RAW10_PACKED means four high bytes followed by their packed two low bits.
 * Neither format may be inferred from the MIPI on-wire data type alone.
 */
enum a733_camera_pixel
{
  A733_CAMERA_RGB24 = 1,
  A733_CAMERA_NV12,
  A733_CAMERA_RAW10_PACKED,
  A733_CAMERA_RAW10_LE16
};

enum a733_camera_bayer
{
  A733_CAMERA_BAYER_UNKNOWN = 0,
  A733_CAMERA_RGGB,
  A733_CAMERA_GRBG,
  A733_CAMERA_GBRG,
  A733_CAMERA_BGGR
};

enum a733_camera_yuv
{
  A733_CAMERA_YUV_UNKNOWN = 0,
  A733_CAMERA_BT601_LIMITED,
  A733_CAMERA_BT709_LIMITED,
  A733_CAMERA_BT601_FULL,
  A733_CAMERA_BT709_FULL
};

struct a733_camera_image
{
  const uint8_t *data;
  size_t bytes;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  enum a733_camera_pixel pixel;
  enum a733_camera_bayer bayer;
  enum a733_camera_yuv yuv;
  uint32_t sequence;
  uint64_t timestamp_us;
};

/* Keep this descriptor with the frame/result. It does NOT fit the existing
 * 32-byte vision queue and must not be cast to a queue message. No pointers
 * or buffer ownership are transferred by this descriptor.
 */
struct a733_camera_transform
{
  uint32_t source_width;
  uint32_t source_height;
  uint32_t output_width;
  uint32_t output_height;
  uint32_t image_width;
  uint32_t image_height;
  uint32_t pad_x;
  uint32_t pad_y;
  uint32_t sequence;
  uint64_t timestamp_us;
};

struct a733_camera_box
{
  /* Continuous image-edge coordinates, in hundredths of one pixel. */
  int32_t x1;
  int32_t y1;
  int32_t x2;
  int32_t y2;
};

/* Bounded, allocation-free letterbox with independent RGB channels.
 * Output dimensions 1..640; RGB order, row-major, no output row padding.
 * RAW is bilinear Bayer demosaic without AWB/AE/gamma/ISP calibration; do
 * not advertise its colours/detection accuracy as an ISP-qualified image.
 * Caller owns a stable completed source frame for the entire call. Source
 * and output/transform must not overlap. Failure leaves output unchanged.
 */
int a733_camera_letterbox(const struct a733_camera_image *source,
                          uint8_t *output, size_t output_bytes,
                          uint32_t width, uint32_t height, uint8_t padding,
                          struct a733_camera_transform *transform);

/* Undo padding/scaling, clip to source, and reject padding-only boxes.
 * Returns -ESTALE if the detection sequence differs from its source frame.
 * A failure does not publish a box. No float dependency on the RTOS.
 */
int a733_camera_unletterbox(const struct a733_camera_transform *transform,
                            uint32_t sequence,
                            const struct a733_camera_box *model_box,
                            struct a733_camera_box *source_box);

/* CPU preview experiment, NOT calibrated ISP/AE/AWB. Q8 unity=256.
 * Keeps the original RAW and legacy linear model tensor unchanged.
 * Fixed gamma 2.2 LUT, configurable empirical black pedestal/software gain,
 * manual RGB gains, optional 180-degree RGB rotation AFTER demosaic.
 */
struct a733_camera_tone
{
  uint32_t black, gain_q8, red_q8, green_q8, blue_q8, rotation;
};
int a733_camera_tone_valid(const struct a733_camera_tone *tone);
int a733_camera_letterbox_tone(const struct a733_camera_image *source,
  uint8_t *output,size_t bytes,uint32_t width,uint32_t height,uint8_t padding,
  const struct a733_camera_tone *tone,struct a733_camera_transform *transform);

/* v205 CPU-only reusable plan. Owns maps/LUTs, not image buffers. Source and
 * output storage must remain at the same addresses until destroy. A source
 * layout change is rejected before output mutation; sequence/time may vary. */
struct a733_camera_tone_plan;
int a733_camera_tone_plan_create(const struct a733_camera_image *,uint8_t *,
  size_t,uint32_t,uint32_t,uint8_t,const struct a733_camera_tone *,
  struct a733_camera_tone_plan **);
int a733_camera_tone_plan_render(struct a733_camera_tone_plan *,
  const struct a733_camera_image *,struct a733_camera_transform *);
void a733_camera_tone_plan_destroy(struct a733_camera_tone_plan *);
#endif
