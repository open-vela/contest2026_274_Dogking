/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_CSI_CAPTURE_H
#define A733_CSI_CAPTURE_H
#include "a733_imx214_cold.h"
#include "a733_csi_frame_plan.h"
#include "a733_mipi_rx.h"
/* Independent CSI-master physical-bypass candidate. Never changes a shared
 * IOMMU table, enable or bypass register. Rejects an active translated CSI
 * master. The caller supplies a permanent identity-mapped guarded buffer;
 * it must remain resident even when stop/restore fails. No IRQ/ISP firmware.
 */
struct a733_csi_capture
{
  const struct a733_imx214_cold_io *io;
  struct a733_csi_frame_plan plan;
  uint32_t original[32],address[32],mask[32];
  unsigned count,changed,reads,writes,polls;
  uint32_t started,elapsed,last_address,status,parser_status,parser_xy;
  uint32_t cleanup_started,max_pause_ms,max_access_ms,schedule_delays;
  uint32_t iommu_enable,iommu_bypass,iommu_version,mbus,fifo;
  uint32_t final_status,final_parser,final_enable,final_bypass;
  uint32_t sizing_active,length_active,flip_active;
  uint32_t sizing_restored,length_restored,flip_restored;
  int error,cleanup_error;
  bool attempted,owned,armed,ready,completed,cleaning,paused;
  const char *stage;
  struct a733_mipi_fault first_fault,cleanup_fault;
};
uint32_t a733_csi_capture_write_mask(uint32_t);
bool a733_csi_capture_readable(uint32_t);
int a733_csi_capture_prepare(struct a733_csi_capture *,
  const struct a733_imx214_cold_io *,uint64_t physical,size_t bytes);
int a733_csi_capture_check(void *);
int a733_csi_capture_wait(struct a733_csi_capture *);
int a733_csi_capture_finish(struct a733_csi_capture *);
/* Sensor-on experiment: stop parser/DMA and prove drain before CPU reads.
 * Retains clock/address snapshots; rearm only an error-free paused frame. */
int a733_csi_capture_pause(struct a733_csi_capture *);
int a733_csi_capture_rearm(struct a733_csi_capture *);
#endif
