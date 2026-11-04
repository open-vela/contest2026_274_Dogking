/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#ifdef CONFIG_A733_CAMERA_CONTROL_TEST
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sched.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/cache.h>
#include <crypto/sha2.h>
#include "arm64_internal.h"
#include "a733_camera_power_handoff.h"
#include "a733_imx214_cold.h"
#include "a733_imx214_twi3.h"
#include "a733_mipi_rx.h"
#include "a733_csi_capture.h"

volatile struct a733_camera_power_handoff g_a733_camera_power_handoff
  __attribute__((section(".data.camera_power"),aligned(64),used)) =
  {.signature=A733_CAMERA_POWER_SIGNATURE,.abi=A733_CAMERA_POWER_ABI};
volatile struct a733_camera_mode_handoff g_a733_camera_mode_handoff
  __attribute__((section(".data.camera_mode"),aligned(64),used)) =
  {.signature=A733_CAMERA_MODE_SIGNATURE,.abi=1};

/* This is the only enabled camera PE/PK-bank writer in this candidate. The mutex
 * serializes its complete bank RMW operations, report and lifetime state.
 * Future PE/PK GPIO/CSI clients MUST join this ownership broker, not bypass it.
 */
static mutex_t control_lock=NXMUTEX_INITIALIZER;
static struct a733_imx214_cold state;
static struct a733_camera_power_handoff captured;
static bool consumed;
static bool id_trial_active;
static bool mode_trial_active,mode_consumed;
static uint8_t mode_bytes[A733_CAMERA_MODE_BYTES];
static int mode_blob_error;
static int command_error;
static bool link_trial_active,link_attempted,stream_on_attempted;
static int stream_error,stop_error;
static struct a733_mipi_rx receiver;
static struct a733_csi_capture frame;
static bool frame_trial_active,frame_requested,frame_published;
static uint8_t frame_buffer[4u*1024u*1024u]
  __attribute__((section(".bss.camera_frame"),aligned(4096)));
static uint32_t frame_fingerprint,frame_min,frame_max;
static int frame_validation;
static unsigned frame_readers,capture_generation;
static bool repeat_ready,repeat_cycle;
static uint16_t sensor_lines,sensor_exposure,sensor_gain;
#ifdef CONFIG_A733_CAMERA_LIVE_TEST
#define CAMERA_CYCLE_LIMIT 32768u
#else
#define CAMERA_CYCLE_LIMIT 32u
#endif
#ifdef CONFIG_A733_CAMERA_PERF_TEST
#define REPORT_BYTES 5120
#else
#define REPORT_BYTES 4096
#endif

static uint32_t millis(void *p)
{(void)p;return TICK2MSEC(clock_systime_ticks());}
static int pause_ms(void *p,unsigned ms)
{(void)p;return nxsig_usleep(ms*1000);}
static bool readable(uint32_t a)
{
  if(frame_trial_active && a733_csi_capture_readable(a))return true;
  if(link_trial_active && a733_mipi_rx_readable(a))return true;
  return a==0x02002518 || a==0x02000280 || a==0x02000284 ||
    a==0x02000290 || a==0x02003804 || a==0x02002e8c ||
    (id_trial_active && a==0x02513008) ||
    a==0x0251300c || a==0x02513010 || a==0x02513014 || a==0x02513018;
}
static int read32(void *p,uint32_t a,uint32_t *v,unsigned ms)
{
  (void)p;(void)ms;
  if (!readable(a)) return -EACCES;
  if(a>=0x03900000 && a<0x03910000 &&
     (!frame_trial_active || (getreg32(0x0200258c)&7)!=7))return -EACCES;
  if (a>=0x02513000 && a<0x02513100 && (getreg32(0x02002e8c)&0x10001)!=0x10001)
    return -EACCES;
  if(a>=0x05800000 && a<0x05840000 &&
     ((getreg32(0x02003844)&0x10001)!=0x10001 || !(getreg32(0x02003840)&0x80000000)))
    return -EACCES;
  *v=getreg32(a);return 0;
}
static int write32(void *p,uint32_t a,uint32_t v,unsigned ms)
{
  (void)p;(void)ms;
  uint32_t mask=0;
  if(frame_trial_active)mask=a733_csi_capture_write_mask(a);
  if(a>=0x05800000 && a<0x05840000 &&
     (!link_trial_active || (getreg32(0x02003844)&0x10001)!=0x10001 ||
      !(getreg32(0x02003840)&0x80000000)))return -EACCES;
  if(link_trial_active)mask|=a733_mipi_rx_write_mask(a);
  if((a==0x05811518 && link_trial_active) ||
     (frame_trial_active && (a==0x05830254 || a==0x05821044)))
    {
      uint32_t status_mask=a==0x05811518?0x1fff:a733_csi_capture_write_mask(a);
      if(v&~status_mask)return -EACCES;
      putreg32(v,a);
#ifdef __aarch64__
      __asm__ volatile("dsb sy" ::: "memory");
#endif
      return 0;
    }
  if (a==0x02000280) mask=0x00fff000;
  if (a==0x02000284) mask=0xff;
  if (a==0x02000290) mask=0x300;
  if (a==0x02003804) mask=0x87001f1f;
  if (a==0x02002e8c) mask=0x10001;
  if (mask)
    {
      uint32_t moving=link_trial_active?a733_mipi_rx_volatile_mask(a):0;
      if ((getreg32(a)^v)&~(mask|moving)) return -EACCES;
      v&=~moving;
    }
  else
    {
      if (!((a==0x0251300c && (v==0 || v==0x48 ||
              (id_trial_active && (v==0x68 || v==0x4c || v==0x58)))) ||
            (id_trial_active && a==0x02513008 &&
              ((mode_trial_active && v<=255) || v==0x20 || v==0 || v==0x16 || v==0x21)) ||
            (a==0x02513014 && v==0x59) || (a==0x02513018 && v==1)))
        return -EACCES;
      if ((getreg32(0x02002e8c)&0x10001)!=0x10001) return -EACCES;
    }
  putreg32(v,a);
#ifdef __aarch64__
  __asm__ volatile("dsb sy" ::: "memory");
#endif
  return 0;
}
static const struct a733_imx214_cold_io io =
  {.read32=read32,.write32=write32,.delay_ms=pause_ms,.now_ms=millis,
   .firmware_power=captured.values};

static int frame_live_check(void *unused)
{
  (void)unused;int r=a733_mipi_rx_check(&receiver);
  return r?r:a733_csi_capture_check(&frame);
}
static int validate_frame(void)
{
  size_t end=64u+frame.plan.payload_capacity;
  for(unsigned i=0;i<64;++i)
    if(frame_buffer[i]!=0xa5 || frame_buffer[end+i]!=0xa5)return -EFAULT;
  uint32_t hash=2166136261u,lo=1023,hi=0;
  const uint8_t *data=frame_buffer+64;
  for(unsigned i=0;i<frame.plan.active_bytes;i+=2)
    {
      unsigned value=data[i]|(unsigned)data[i+1]<<8;
      if(value>1023)return -EPROTO; /* incomplete/sentinel or unqualified alignment */
      if(value<lo)lo=value;
      if(value>hi)hi=value;
      hash=(hash^value)*16777619u;
    }
  frame_min=lo;frame_max=hi;frame_fingerprint=hash;return 0;
}

static int packet_trial(void *p,struct a733_imx214_twi3 *bus)
{
  (void)p;
  link_attempted=true;
  uint8_t settings[2];
  int ret=a733_imx214_twi3_trial_read(bus,0x0340,settings,2,30);
  if(!ret)sensor_lines=(uint16_t)settings[0]<<8|settings[1];
  if(!ret)ret=a733_imx214_twi3_trial_read(bus,0x0202,settings,2,30);
  if(!ret)sensor_exposure=(uint16_t)settings[0]<<8|settings[1];
  if(!ret)ret=a733_imx214_twi3_trial_read(bus,0x0204,settings,2,30);
  if(!ret)sensor_gain=(uint16_t)settings[0]<<8|settings[1];
  if(!ret)ret=a733_mipi_rx_prepare(&receiver,&io);
  if(!ret && frame_requested)
    {
      uintptr_t physical=(uintptr_t)frame_buffer;
      if(physical<0x40200000 || physical>0x48000000-sizeof(frame_buffer))ret=-ERANGE;
      else
        {
          memset(frame_buffer,0xa5,sizeof(frame_buffer));
          memset(frame_buffer+64,0xff,3840u*1088u);
          up_clean_dcache(physical,physical+sizeof(frame_buffer));
          ret=a733_csi_capture_prepare(&frame,&io,physical,sizeof(frame_buffer));
        }
    }
  if(!ret)
    {
      stream_on_attempted=true;
      ret=stream_error=a733_imx214_twi3_packet_stream(bus,
        frame_requested?frame_live_check:a733_mipi_rx_check,&receiver,true);
      if(!ret)ret=frame_requested?a733_csi_capture_wait(&frame):a733_mipi_rx_observe(&receiver);
      if(!ret && frame_requested)ret=a733_mipi_rx_snapshot(&receiver);
      /* Even partial ON writes require bounded OFF, unless the TWI engine
       * has quarantined ownership. The outer control path then asserts reset.
       */
      if(bus->packet_stream_attempted && !bus->quarantined)
        stop_error=a733_imx214_twi3_packet_stream(bus,NULL,NULL,false);
      else if(bus->packet_stream_attempted)stop_error=-EIO;
      if(!ret)ret=stop_error;
    }
  if(frame.attempted)
    {
      int cleanup=a733_csi_capture_finish(&frame);
      if(!ret)ret=cleanup;
      if(!ret)ret=frame.error;
      if(frame.owned){bus->quarantined=true;return ret?ret:-EIO;}
    }
  int cleanup=a733_mipi_rx_finish(&receiver);
  if(!ret)ret=cleanup;
  if(receiver.owned)bus->quarantined=true;
  if(!ret && frame_requested && frame.completed && !receiver.owned)
    {
      uintptr_t physical=(uintptr_t)frame_buffer;
      up_invalidate_dcache(physical,physical+sizeof(frame_buffer));
      ret=frame_validation=validate_frame();
      uint32_t first=frame_fingerprint;
      if(!ret)ret=pause_ms(NULL,10);
      up_invalidate_dcache(physical,physical+sizeof(frame_buffer));
      if(!ret)ret=frame_validation=validate_frame();
      if(!ret && first!=frame_fingerprint)ret=frame_validation=-ESTALE;
      /* Outer sensor/control restoration is still checked before publishing. */
    }
  return ret;
}

static int capture_handoff(void)
{
  if (consumed)
    {
      /* Reuse ONLY a fully restored successful RAW cycle. No failure reset,
       * new PMIC access, erased fault, or arbitrary rearm CLI is permitted. */
      if(!repeat_ready || !frame_published || capture_generation>=CAMERA_CYCLE_LIMIT)return -EALREADY;
      if(__atomic_load_n(&frame_readers,__ATOMIC_ACQUIRE))return -EBUSY;
      if(state.state!=A733_IMX214_COLD_PARKED || state.error || state.cleanup_error ||
         state.bus_quarantined || state.controls_owned || frame.owned || receiver.owned ||
         frame.error || frame.cleanup_error || receiver.error || receiver.cleanup_error ||
         !frame.completed || frame_validation || stream_error || stop_error)return -EIO;
      repeat_ready=false;repeat_cycle=true;frame_published=false;
      memset(&frame,0,sizeof(frame));memset(&receiver,0,sizeof(receiver));
      frame_validation=0;frame_requested=false;
      link_attempted=stream_on_attempted=false;stream_error=stop_error=0;
      return 0;
    }
  /* One attempt per freshly loaded Image. Clear even an invalid record;
   * failure cannot be bypassed with a retry or a plain CLI boolean.
   */
  consumed=true;
  repeat_ready=false;repeat_cycle=false;
  const volatile unsigned char *src=(const volatile unsigned char*)&g_a733_camera_power_handoff;
  unsigned char *dst=(unsigned char*)&captured;
  for (unsigned i=0;i<sizeof(captured);++i) dst[i]=src[i];
  g_a733_camera_power_handoff.ready=0;
  if (captured.signature!=A733_CAMERA_POWER_SIGNATURE ||
      captured.abi!=A733_CAMERA_POWER_ABI || captured.ready!=A733_CAMERA_POWER_READY)
    return -ENODATA;
  for (unsigned i=0;i<sizeof(captured.reserved);++i)
    if (captured.reserved[i]) return -EPROTO;
  return 0;
}

static int capture_mode(void)
{
  if (mode_consumed && !repeat_cycle) return -EALREADY;
  if(mode_consumed)goto verify_cached;
  mode_consumed=true;
  uint32_t ready=g_a733_camera_mode_handoff.ready;
  g_a733_camera_mode_handoff.ready=0;
  if (g_a733_camera_mode_handoff.signature!=A733_CAMERA_MODE_SIGNATURE ||
      g_a733_camera_mode_handoff.abi!=1 || ready!=A733_CAMERA_MODE_READY ||
      g_a733_camera_mode_handoff.bytes!=A733_CAMERA_MODE_BYTES) return -ENODATA;
  for(unsigned i=0;i<28;++i)if(g_a733_camera_mode_handoff.reserved[i])return -EPROTO;
  for(unsigned i=0;i<sizeof(mode_bytes);++i)mode_bytes[i]=g_a733_camera_mode_handoff.data[i];
verify_cached:;
  static const uint8_t expected[32]={0xa7,0xb2,0x10,0x2b,0xa0,0x5c,0x51,0xde,
    0x1a,0x8a,0x76,0x02,0x15,0x25,0x05,0xf4,0xfe,0xed,0xbe,0xb3,0xec,0x9f,0xac,0xdd,
    0xa7,0x07,0x10,0xf9,0x63,0x9b,0xb2,0xaf};
  SHA2_CTX sha;uint8_t sum[32];
  sha256init(&sha);sha256update(&sha,mode_bytes,sizeof(mode_bytes));sha256final(sum,&sha);
  return memcmp(sum,expected,32) ? -EBADMSG : 0;
}

#ifdef CONFIG_A733_CAMERA_PERF_TEST
#include "a733_camera_stream.inc"
#endif

static int control_open(struct file *fp)
{
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if((fp->f_oflags&O_ACCMODE)==O_WRONLY){fp->f_priv=NULL;return 0;}
#endif
  const char *previous=fp->f_priv;
  fp->f_priv=NULL;
  char *report=calloc(1,REPORT_BYTES);
  if (!report) return -ENOMEM;
  if (previous)
    {memcpy(report,previous,REPORT_BYTES);fp->f_priv=report;return 0;}
  int ret=nxmutex_trylock(&control_lock);
  if (ret) {free(report);return ret;}
  int report_size=snprintf(report,REPORT_BYTES,
    "camera-control-v203: V1.11/J5 guarded repeat RAW + read-only exposure diagnostics\n"
    "state=%u stage=%s error=%d cleanup=%d command-error=%d owned=%u consumed=%u\n"
    "power: source=same-boot-U-Boot-image-handoff ready=%08lx bytes=%02x/%02x/%02x/%02x/%02x/%02x\n"
    "runtime-PMIC-access=NONE rails-retained=ASSUMED physical-voltage=NOT-MEASURED\n"
    "MCLK: configured=%lu Hz measured=UNKNOWN; prepare holds reset/STBY LOW\n"
    "reads=%u writes=%u polls=%u elapsed-ms=%lu last-address=%08lx\n"
    "id-experiment: attempted=%u reads=%u chip-id=%04x match=%u error=%d polls=%u bus-quarantined=%u\n"
    "mode-experiment: attempted=%u blob-error=%d writes=%lu reads=%lu verified=%u/14 last-reg=%04x error=%d\n"
    "packet-experiment: attempted=%u stage=%s receiver-error=%d cleanup=%d owned=%u ready=%u\n"
    "source: on-attempted=%u on-error=%d stop-error=%d; %s\n"
    "packets: raw-headers=%u fs-observations=%u fe-observations=%u status=%08lx errors=%08lx last-header=%08lx\n"
    "receiver: reads=%u writes=%u elapsed=%lu last-address=%08lx phy-debug=%08lx phy-frequency=%08lx pll-ref=%08lx pll-peri=%08lx\n"
    "PK-before: cfg1=%08lx cfg2=%08lx drv1=%08lx drv2=%08lx; target=PK10..19/mux2/10mA; unrelated PK fields preserved\n"
    "receiver-first: error=%d address=%08lx mask=%08lx expected=%08lx observed=%08lx\n"
    "receiver-cleanup: error=%d address=%08lx mask=%08lx expected=%08lx observed=%08lx\n"
    "frame: requested=%u attempted=%u stage=%s error=%d cleanup=%d owned=%u completed=%u published=%u validation=%d\n"
    "frame-data: address=%08lx bytes=%lu stride=3840 format=RAW10-LE16 vendor-BGGR min=%lu max=%lu fingerprint=%08lx status=%08lx parser=%08lx xy=%08lx fifo=%08lx\n"
    "frame-mapping: CPU-identity CSI-master1 version=%08lx enable=%08lx bypass=%08lx mbus=%08lx; NO-IOMMU-TABLE/RESET/BYPASS-WRITES\n"
    "frame-first: error=%d address=%08lx mask=%08lx expected=%08lx observed=%08lx\n"
    "frame-cleanup: error=%d address=%08lx mask=%08lx expected=%08lx observed=%08lx\n"
    "frame-final: dma=%08lx parser=%08lx mapping-enable=%08lx bypass=%08lx\n"
    "frame-schedule-v212: max-pause-ms=%lu max-access-ms=%lu delays-over21=%lu operation-deadline-ms=2000 cleanup-deadline-ms=2000\n"
    "frame-sizing: active-top=%08lx length=%08lx flip=%08lx restored-top=%08lx length=%08lx flip=%08lx\n"
    "strict-qualification=NONE; RAW requires published=1; NO-ISP-IMAGE-TUNING\n"
    "repeat-v203: generation=%u readers=%u ready=%u max=%u same-boot-cached-power; any failed cycle requires reboot\n"
    "sensor-readback: frame-lines=%u exposure-lines=%u analog-code=%04x (read-only, NO AE/gain writes)\n"
    "control: prepare | calibrate | normal-clock | id-experiment OR mode-experiment OR link-experiment OR frame-experiment | park\n"
    "normal boot has no handoff: prepare returns ENODATA with ZERO MMIO; see camera boot-help\n",
    (unsigned)state.state,state.stage?state.stage:"idle",state.error,state.cleanup_error,command_error,
    state.controls_owned,consumed,(unsigned long)captured.ready,
    captured.values[0],captured.values[1],captured.values[2],captured.values[3],
    captured.values[4],captured.values[5],(unsigned long)state.configured_mclk_hz,
    state.reads,state.writes,state.polls,(unsigned long)state.elapsed_ms,
    (unsigned long)state.last_address,state.id_trial_attempted,state.id_trial.reads,
    state.id_trial.chip_id,state.id_trial.identity_match,state.id_trial.error,
    state.id_polls,state.bus_quarantined,state.mode_trial_attempted,mode_blob_error,
    (unsigned long)state.mode_trial.writes,(unsigned long)state.mode_trial.reads,
    state.mode_trial.verified_registers,state.mode_trial.last_register,state.mode_trial.error,
    link_attempted,receiver.stage?receiver.stage:"idle",receiver.error,receiver.cleanup_error,
    receiver.owned,receiver.ready,stream_on_attempted,stream_error,stop_error,
    frame_requested?"bounded RAW DMA to private frame pool":"collect=500ms NO-DDR-WRITES",
    receiver.raw_headers,receiver.fs,receiver.fe,(unsigned long)receiver.status,
    (unsigned long)receiver.errors,(unsigned long)receiver.header,receiver.reads,receiver.writes,
    (unsigned long)receiver.elapsed,(unsigned long)receiver.last_address,
    (unsigned long)receiver.phy_debug,(unsigned long)receiver.phy_frequency,
    (unsigned long)receiver.pll_ref,(unsigned long)receiver.pll_peri,
    (unsigned long)receiver.original[22],(unsigned long)receiver.original[23],
    (unsigned long)receiver.original[24],(unsigned long)receiver.original[25],
    receiver.first_fault.error,(unsigned long)receiver.first_fault.address,
    (unsigned long)receiver.first_fault.mask,(unsigned long)receiver.first_fault.expected,
    (unsigned long)receiver.first_fault.observed,receiver.cleanup_fault.error,
    (unsigned long)receiver.cleanup_fault.address,(unsigned long)receiver.cleanup_fault.mask,
    (unsigned long)receiver.cleanup_fault.expected,(unsigned long)receiver.cleanup_fault.observed,
    frame_requested,frame.attempted,frame.stage?frame.stage:"idle",frame.error,frame.cleanup_error,
    frame.owned,frame.completed,frame_published,frame_validation,
    (unsigned long)frame.plan.payload_iova,(unsigned long)frame.plan.active_bytes,
    (unsigned long)frame_min,(unsigned long)frame_max,(unsigned long)frame_fingerprint,
    (unsigned long)frame.status,(unsigned long)frame.parser_status,(unsigned long)frame.parser_xy,
    (unsigned long)frame.fifo,(unsigned long)frame.iommu_version,(unsigned long)frame.iommu_enable,
    (unsigned long)frame.iommu_bypass,(unsigned long)frame.mbus,
    frame.first_fault.error,(unsigned long)frame.first_fault.address,(unsigned long)frame.first_fault.mask,
    (unsigned long)frame.first_fault.expected,(unsigned long)frame.first_fault.observed,
    frame.cleanup_fault.error,(unsigned long)frame.cleanup_fault.address,(unsigned long)frame.cleanup_fault.mask,
    (unsigned long)frame.cleanup_fault.expected,(unsigned long)frame.cleanup_fault.observed,
    (unsigned long)frame.final_status,(unsigned long)frame.final_parser,
    (unsigned long)frame.final_enable,(unsigned long)frame.final_bypass,
    (unsigned long)frame.max_pause_ms,(unsigned long)frame.max_access_ms,
    (unsigned long)frame.schedule_delays,
    (unsigned long)frame.sizing_active,(unsigned long)frame.length_active,(unsigned long)frame.flip_active,
    (unsigned long)frame.sizing_restored,(unsigned long)frame.length_restored,(unsigned long)frame.flip_restored,
    capture_generation,__atomic_load_n(&frame_readers,__ATOMIC_ACQUIRE),repeat_ready,CAMERA_CYCLE_LIMIT,
    sensor_lines,sensor_exposure,sensor_gain);
 #ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(report_size>0 && report_size<REPORT_BYTES)
    report_size+=snprintf(report+report_size,REPORT_BYTES-(size_t)report_size,
      "stream-v206: active=%u stop=%u pending=%u working=%u frames=%u delivered=%u error=%d cleanup=%d paused=%u; sensor-on gated-SCAP, NOT cyclic-DMA; failed capture requires reboot\n",
      stream.active,stream.stop,stream.pending,stream.working,stream.frames,stream.delivered,stream.error,stream.cleanup,frame.paused);
 #endif
  nxmutex_unlock(&control_lock);
  if(report_size<0 || report_size>=REPORT_BYTES){free(report);return -EOVERFLOW;}
  fp->f_priv=report;return 0;
}
static int control_close(struct file *fp)
{free(fp->f_priv);fp->f_priv=NULL;return 0;}
static ssize_t control_read(struct file *fp,char *buf,size_t n)
{
  if (!fp->f_priv || fp->f_pos<0 || (n && !buf)) return -EINVAL;
  size_t length=strlen(fp->f_priv);
  if ((uint64_t)fp->f_pos>=length || !n) return 0;
  size_t left=length-(size_t)fp->f_pos;if(n>left)n=left;
  memcpy(buf,(char*)fp->f_priv+fp->f_pos,n);fp->f_pos+=n;return n;
}
static ssize_t control_write(struct file *fp,const char *buf,size_t n)
{
  (void)fp;
  if (!buf || !n || n>16) return -EINVAL;
  size_t len=n;if(buf[len-1]=='\n')--len;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(len>=7 && !memcmp(buf,"stream-",7))
    {
      int lock=nxmutex_trylock(&control_lock);if(lock)return lock;
      int r=stream_command(buf,len);
      nxmutex_unlock(&control_lock);return r?r:(ssize_t)n;
    }
#endif
  bool prepare=len==7 && !memcmp(buf,"prepare",7);
  bool park=len==4 && !memcmp(buf,"park",4);
  bool calibrate=len==9 && !memcmp(buf,"calibrate",9);
  bool normal=len==12 && !memcmp(buf,"normal-clock",12);
  bool identify=len==13 && !memcmp(buf,"id-experiment",13);
  bool mode=len==15 && !memcmp(buf,"mode-experiment",15);
  bool link=len==15 && !memcmp(buf,"link-experiment",15);
  bool capture=len==16 && !memcmp(buf,"frame-experiment",16);
  if (!prepare && !park && !calibrate && !normal && !identify && !mode && !link && !capture) return -EINVAL;
  int ret=nxmutex_trylock(&control_lock);
  if (ret) return ret;
#ifdef CONFIG_A733_CAMERA_PERF_TEST
  if(stream.active){nxmutex_unlock(&control_lock);return -EBUSY;}
#endif
  if (prepare)
    {
      ret=state.controls_owned ? -EBUSY : state.state==A733_IMX214_COLD_FAULT ? -EIO : capture_handoff();
      if (!ret) ret=a733_imx214_cold_prepare(&state,&io);
    }
  else if (park) ret=(frame.owned || receiver.owned) ? -EBUSY : a733_imx214_cold_park(&state,&io);
  else if (identify || mode || link || capture)
    {
      ret=0;
      if (mode || link || capture)
        {
          if(state.state!=A733_IMX214_COLD_PREPARED || !state.controls_owned)
            ret=-EACCES;
          else ret=mode_blob_error=capture_mode();
        }
      if(ret){repeat_cycle=false;command_error=ret;nxmutex_unlock(&control_lock);return ret;}
      repeat_ready=false;
      id_trial_active=true;
      mode_trial_active=(mode || link || capture) && !ret;
      link_trial_active=(link || capture) && !ret;
      frame_trial_active=capture && !ret;
      frame_requested=frame_trial_active;
      struct a733_imx214_cold_io operation=io;
      if(link_trial_active)operation.packet_trial=packet_trial;
      if (!ret) ret=(mode || link || capture) ? a733_imx214_cold_trial_mode(&state,&operation,mode_bytes,sizeof(mode_bytes)) :
                           a733_imx214_cold_trial_id(&state,&io);
      frame_published=frame_requested && !ret && frame.completed && !frame.owned &&
        !receiver.owned && !state.controls_owned && !state.cleanup_error && !frame_validation;
      if(frame_published)
        {frame.stage="RAW-frame-valid-after-stop-and-guard-check";state.stage="RAW-frame-validated-controls-restored";
         ++capture_generation;repeat_ready=true;}
      repeat_cycle=false;
      id_trial_active=false;
      mode_trial_active=false;
      link_trial_active=false;
      frame_trial_active=false;
    }
  else ret=a733_imx214_cold_clock(&state,&io,calibrate);
  command_error=ret;
  nxmutex_unlock(&control_lock);
  return ret ? ret : (ssize_t)n;
}
static const struct file_operations control_ops =
  {.open=control_open,.close=control_close,.read=control_read,.write=control_write};
static int frame_open(struct file *fp)
{
  int ret=nxmutex_trylock(&control_lock);if(ret)return ret;
  if(!frame_published)ret=-ENODATA;
  else if(__atomic_load_n(&frame_readers,__ATOMIC_ACQUIRE)>=16)ret=-EMFILE;
  else {__atomic_add_fetch(&frame_readers,1,__ATOMIC_RELEASE);fp->f_priv=(void *)(uintptr_t)1;}
  nxmutex_unlock(&control_lock);return ret;
}
static int frame_close(struct file *fp)
{
  if(fp->f_priv){fp->f_priv=NULL;__atomic_sub_fetch(&frame_readers,1,__ATOMIC_RELEASE);}
  return 0;
}
static ssize_t frame_read(struct file *fp,char *buf,size_t n)
{
  if(fp->f_pos<0 || (n && !buf))return -EINVAL;
  int r=nxmutex_trylock(&control_lock);if(r)return r;
  if(!frame_published){nxmutex_unlock(&control_lock);return -ENODATA;}
  if((uint64_t)fp->f_pos>=frame.plan.active_bytes){nxmutex_unlock(&control_lock);return 0;}
  size_t left=frame.plan.active_bytes-(size_t)fp->f_pos;if(n>left)n=left;
  memcpy(buf,frame_buffer+64+(size_t)fp->f_pos,n);fp->f_pos+=n;
  nxmutex_unlock(&control_lock);return n;
}
static const struct file_operations frame_ops={.open=frame_open,.close=frame_close,.read=frame_read};
int a733_camera_control_initialize(void)
{
  /* Registration only. No MMIO, SMC, PMIC bus or handoff consumption. */
  int r=register_driver("/dev/a733-camera-control",&control_ops,0660,NULL);
  return r?r:register_driver("/dev/a733-camera-frame",&frame_ops,0440,NULL);
}
#endif
