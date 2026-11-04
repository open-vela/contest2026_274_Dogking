/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __INCLUDE_NUTTX_NPU_A733_TENSOR_H
#define __INCLUDE_NUTTX_NPU_A733_TENSOR_H
#include <stdint.h>
#ifdef __NuttX__
#include <nuttx/config.h>
#endif

/* Checked capacities, not ABI layout changes. Default values remain those
 * of the passed keyword/vocoder transport. All larger bounds are opt-in. */
#ifdef CONFIG_A733_NPU_GENERAL_ASR
#define A733_TENSOR_IO_LIMIT (8u * 1024u * 1024u)
#define A733_TENSOR_FILE_LIMIT (64u * 1024u * 1024u)
#define A733_TENSOR_DMA_LIMIT (64u * 1024u * 1024u)
#else
#define A733_TENSOR_IO_LIMIT (1024u * 1024u)
#define A733_TENSOR_FILE_LIMIT (32u * 1024u * 1024u)
#define A733_TENSOR_DMA_LIMIT (24u * 1024u * 1024u)
#endif

/* Optional extension; original A7PM/YOLO ABI is unchanged. A7TV is a checked,
 * board-prepared artifact, NOT an NBG parser/compiler or a security sandbox.
 * Only trusted, locally prepared commands for CID 1000003b may be used.
 * No raw user addresses, floating point parameters or implicit dtype casts.
 */
#define A733_TENSOR_ABI 1u
#define A733_TENSOR_MAX_IO 64u
#define A733_TENSOR_MAX_REGIONS 128u
#define A733_TENSOR_MAGIC UINT32_C(0x56543741) /* bytes A7TV */
#define A733_TENSOR_FILE_VERSION 1u
#define A733_TENSOR_CID UINT32_C(0x1000003b)
#define A733_TENSOR_PATH 128u
#define A733_TENSOR_LOAD 1u
#define A733_TENSOR_OUTPUT 2u
#define A733_TENSOR_COMMAND 4u
#define A733_TENSOR_OPAQUE UINT32_MAX
#define A733_TENSOR_VERIFY_GOLDEN 1u
#define A733_TENSOR_RESIDENT 2u
#define A733_TENSOR_FULL_CLEAN 4u /* A/B diagnostic; never disables validation */
#define A733_TENSOR_CRC_LEGACY 8u /* A/B driver CRC only; same bytes/seed/checks */
#define A733_TENSOR_PROFILE_PHASES 9u
/* validate, rebuild+CRC, map, clean, submit+wait, invalidate+check, commit,
 * restore, total. Times are wall microseconds, not pure accelerator cycles. */
struct a733_tensor_profile_s
{
  uint32_t version;
  int32_t status;
  uint32_t committed, reserved;
  uint64_t us[A733_TENSOR_PROFILE_PHASES];
  uint64_t clean_bytes, table_clean_bytes, rebuild_bytes;
};

struct a733_tensor_header_s
{
  uint32_t magic, version, header_size, cid, total_size;
  uint32_t regions, inputs, outputs, command_region, command_offset;
  uint32_t manifest_crc32, flags;
  char name[32];
  uint32_t reserved[12];
};
struct a733_tensor_region_s
{
  uint32_t vip, file_offset, bytes, crc32, flags;
  uint32_t reserved[3];
};
struct a733_tensor_desc_s
{
  uint32_t region, offset, bytes, crc32, format, quant, rank;
  uint32_t dims[4];
  uint32_t reserved[5];
};
struct a733_tensor_ref_s
{
  uint32_t handle, offset, bytes, reserved;
};
/* Query validates header, manifest, tensor slices and command-tail space;
 * RUN additionally streams/checks all payload CRCs before any hardware work.
 * Query is NOT proof of an executable or hash-verified NBG conversion.
 */
struct a733_tensor_query_s
{
  uint32_t version;
  char package_path[A733_TENSOR_PATH];
  int32_t status;
  uint32_t inputs, outputs;
  uint32_t input_bytes[A733_TENSOR_MAX_IO];
  uint32_t output_bytes[A733_TENSOR_MAX_IO];
  uint32_t input_crc32[A733_TENSOR_MAX_IO];
  uint32_t output_crc32[A733_TENSOR_MAX_IO];
};
/* Single-owner synchronous call. Referenced buffers must be allocated on the
 * SAME open fd. Caller relinquishes access to them until return. Descriptor
 * lengths must be exact native padded lengths, including recurrent states.
 * Outputs are committed only after ALL hardware/guard/CRC checks succeed;
 * on failure their bytes remain unchanged, committed=0. No automatic retry.
 * Hardware/guard errors quarantine this experimental backend until reboot.
 * RESIDENT requires PREPARE on the same fd and exact path. PREPARE validates
 * and snapshots original payloads in ordinary RAM (64 MiB global budget).
 * RUN still reconstructs the DMA working image; never reuses mutated output
 * as original weights. Without this flag the v157 file-per-call path remains.
 */
struct a733_tensor_run_s
{
  uint32_t version, flags, timeout_ms, inputs, outputs;
  char package_path[A733_TENSOR_PATH];
  struct a733_tensor_ref_s input[A733_TENSOR_MAX_IO];
  struct a733_tensor_ref_s output[A733_TENSOR_MAX_IO];
  int32_t status, hardware_status;
  uint32_t committed, irq_value, idle, polls;
  uint32_t output_crc32[A733_TENSOR_MAX_IO];
  uint32_t output_changed[A733_TENSOR_MAX_IO];
};
#endif
