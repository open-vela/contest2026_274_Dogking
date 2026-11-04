/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_TENSOR_CLIENT_H
#define A733_TENSOR_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define A733_TENSOR_CLIENT_IO 64u
struct a733_tensor_client;
struct a733_tensor_client_contract {
  uint32_t inputs, outputs;
  uint32_t input_bytes[A733_TENSOR_CLIENT_IO];
  uint32_t output_bytes[A733_TENSOR_CLIENT_IO];
};
struct a733_tensor_client_input { const void *data; size_t bytes; };
struct a733_tensor_client_output { void *data; size_t bytes; };
#define A733_TENSOR_CLIENT_PHASES 9u
struct a733_tensor_client_profile {
  uint64_t us[A733_TENSOR_CLIENT_PHASES];
  uint64_t clean_bytes, table_clean_bytes, rebuild_bytes;
};
/* NuttX-compatible raw CRC (seed unchanged, no complement), shared with driver.
 * Non-null memory is required for nonzero bytes. Optional performance builds
 * use runtime-qualified ARM CRC with exact portable fallback. */
uint32_t a733_tensor_client_crc32(const void *data, size_t bytes, uint32_t seed);
int a733_tensor_client_full_clean(struct a733_tensor_client *state, int enable);
/* Restore official byte-table CRC in RUN only; all checks still mandatory. */
int a733_tensor_client_crc_legacy(struct a733_tensor_client *state, int enable);
int a733_tensor_client_get_profile(struct a733_tensor_client *state,
                                  struct a733_tensor_client_profile *profile);
/* Public synchronous single-owner BSP client. Disable asynchronous task cancellation
 * during calls. No locks/reentrancy inside an object; separate objects own
 * separate fd/handles. open checks an EXACT expected padded-byte contract;
 * callers must also use a trusted/hash-verified prepared artifact. No NBG
 * loading, no network/CPU fallback, no recognition or audio side effects.
 * Without prepare, weights reload per run. Explicit prepare snapshots original
 * model data in RAM until close; RUN still rebuilds the DMA working image.
 */
int a733_tensor_client_open(const char *package,
                         const struct a733_tensor_client_contract *expected,
                         struct a733_tensor_client **out);
/* Explicit opt-in after open, before execute. Holds a verified model snapshot
 * until close. Failure poisons the client; no implicit fallback/retry. */
int a733_tensor_client_prepare(struct a733_tensor_client *state);
int a733_tensor_client_execute(struct a733_tensor_client *state,
                            const struct a733_tensor_client_input *input,
                            const struct a733_tensor_client_output *output,
                            uint32_t timeout_ms, int verify_capture);
/* A failed ioctl poisons this object; callers must not retry automatically.
 * Input preflight errors do not poison. Application output bytes remain
 * unchanged on failure. close is required; it never resets the hardware. */
int a733_tensor_client_close(struct a733_tensor_client *state);
#ifdef __cplusplus
}
#endif
#endif
