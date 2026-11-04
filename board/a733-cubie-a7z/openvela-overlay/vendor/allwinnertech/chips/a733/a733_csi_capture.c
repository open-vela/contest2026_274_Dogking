/* SPDX-License-Identifier: Apache-2.0 */
/* Independently implemented from sun60iw2 register/lifecycle facts.
 * RAW format in the BSP bypasses ISP/scaler execution. Configure the CSI
 * bridge route, parser1/channel0 and BK0/channel0; do not assume the older
 * sun50iw9-only dma_input_select() exists on this chip. See v197 provenance.
 */
#include "a733_csi_capture.h"
#include <errno.h>
#include <string.h>
#define P 0x05821000u
#define D 0x05830000u
#define VALID 0x002fe0e7u
#define ERRORS 0x002f8044u
struct entry {uint32_t a,m,v;};
enum {FIRST_DMA=8,ADDRESS_FIRST=15,ADDRESS_LAST=17,DMA_ENABLE=11,
      PARSER_ENABLE=19,PARSER_CAPTURE=22,DMA_TOP_IRQ=10,DMA_IRQ=18,PARSER_IRQ=26};
static const struct entry settings[]={
  {0x0200258c,7,7}, /* IOMMU0 access clocks, no reset/table writes */
  {0x02003860,0x8700001f,0x81000001}, /* ISP bridge source: PERI0-480M /2 */
  {0x05800004,2,2}, /* parser1 */
  {0x05800008,0x00111311,0x00011011}, /* bridge0, no ISP execution */
  {0x0580000c,0x00310101,0x00010101}, /* BK0/VIPP0/post0 */
  {0x05800800,0x00111108,0x00011008}, /* owned bridge fields */
  {0x05800830,15,4}, /* parser1/ch0 -> ISP0 input0 */
  {0x058008a0,15,0}, /* ISP0/ch0 -> output0; RAW bypass */
  /* Follow the BSP RAW automatic-size path. Bits29/28/30 stay zero.
   * BUF_LEN and FLIP_SIZE are derived views in this mode: observe them,
   * NEVER program, compare as software storage, or restore their snapshots.
   * This also preserves any hidden software-mode latches of the old owner. */
  {D,0x70001001,1}, {D+4,1,0}, {D+0x100,7,0},
  {D+0x200,0x183,0}, {D+0x204,0x003f3ffc,0x00010800}, /* RAW10, FIELD_EITHER */
  {D+0x210,0x3fff3fff,1920u<<16},
  {D+0x214,0x3fff3fff,1080u<<16},
  {D+0x220,0xffffffff,0}, /* address patched from owned plan */
  {D+0x228,0xffffffff,0}, {D+0x230,0xffffffff,0},
  {D+0x250,VALID,0},
  {P,0x80018007,0x80008003}, {P+4,0x00f00000,0},
  {P+8,0xe0,0}, {P+0xc,0x3f3f3f3f,0},
  {P+0x24,15,0}, {P+0x28,0x1fff1fff,1920u<<16},
  {P+0x2c,0x1fff1fff,1080u<<16}, {P+0x40,7,0}
};
_Static_assert(sizeof(settings)/sizeof(settings[0])<=32,"CSI snapshot capacity");
static unsigned entries(void){return sizeof(settings)/sizeof(settings[0]);}
uint32_t a733_csi_capture_write_mask(uint32_t a)
{
  for(unsigned i=0;i<entries();++i)if(settings[i].a==a)return settings[i].m;
  if(a==D+0x254)return VALID;
  if(a==P+0x44)return 7;
  return 0;
}
bool a733_csi_capture_readable(uint32_t a)
{
  return a733_csi_capture_write_mask(a) || a==0x020025e4 ||
    a==0x03900000 || a==0x03900014 || a==0x03900604 ||
    a==D+8 || a==D+0x24c || a==D+0x238 || a==D+0x23c || a==P+0x34 || a==P+0x3c;
}
static int fault(struct a733_csi_capture *s,int r,uint32_t a,
                 uint32_t m,uint32_t expected,uint32_t observed)
{
  struct a733_mipi_fault *f=s->cleaning?&s->cleanup_fault:&s->first_fault;
  if(r && !f->error)*f=(struct a733_mipi_fault){a,m,expected,observed,r};
  return r;
}
static int access32(struct a733_csi_capture *s,uint32_t a,uint32_t *v,bool write)
{
  if((!s->cleaning && (++s->polls>100000 || s->io->now_ms(s->io->context)-s->started>2000)) ||
     (s->cleaning && s->io->now_ms(s->io->context)-s->cleanup_started>2000))
    return fault(s,-ETIMEDOUT,a,0,0,0);
  s->last_address=a;uint32_t start=s->io->now_ms(s->io->context);
  int r;
  if(write){++s->writes;r=s->io->write32(s->io->context,a,*v,10);}
  else{++s->reads;r=s->io->read32(s->io->context,a,v,10);}
  if(r>0)r=-EPROTO;
  uint32_t elapsed=s->io->now_ms(s->io->context)-start;
  if(elapsed>s->max_access_ms)s->max_access_ms=elapsed;
  /* A completed MMIO callback can have been preempted. Its wall duration is
   * not a bus timeout. Keep callback errors and the whole-operation deadline. */
  if(!r && s->io->now_ms(s->io->context)-
      (s->cleaning?s->cleanup_started:s->started)>2000)r=-ETIMEDOUT;
  return fault(s,r,a,0,0,0);
}
static int rd(struct a733_csi_capture *s,uint32_t a,uint32_t *v)
{return access32(s,a,v,false);}
static int wr(struct a733_csi_capture *s,uint32_t a,uint32_t v)
{return access32(s,a,&v,true);}
static int update(struct a733_csi_capture *s,uint32_t a,uint32_t mask,uint32_t value)
{
  uint32_t old=0,now=0;int r=rd(s,a,&old);
  if(!r)r=wr(s,a,(old&~mask)|(value&mask));
  if(!r)r=rd(s,a,&now);
  if(!r && (now&mask)!=(value&mask))r=fault(s,-EIO,a,mask,value,now);
  return r;
}
static int pause_ms(struct a733_csi_capture *s)
{
  uint32_t before=s->io->now_ms(s->io->context);
  int r=s->io->delay_ms(s->io->context,1);
  uint32_t after=s->io->now_ms(s->io->context)-before;
  if(r>0)r=-EPROTO;
  if(after>s->max_pause_ms)s->max_pause_ms=after;
  if(after>21)++s->schedule_delays;
  /* nxsig_usleep is a minimum delay, NOT a promise to resume within 21ms.
   * v209 pinned the broker beside ~35ms SPI work. Do not reject a completed
   * frame solely for scheduler latency. A stalled clock and real 2s expiry
   * still fail; iteration limits remain in the caller. */
  if(!r && (after<1 || s->io->now_ms(s->io->context)-
      (s->cleaning?s->cleanup_started:s->started)>2000))r=-ETIMEDOUT;
  return fault(s,r,s->last_address,0,0,0);
}
int a733_csi_capture_prepare(struct a733_csi_capture *s,
  const struct a733_imx214_cold_io *io,uint64_t physical,size_t bytes)
{
  if(!s || !io || !io->read32 || !io->write32 || !io->now_ms || !io->delay_ms)return -EINVAL;
  if(s->attempted || s->owned)return -EALREADY;
  memset(s,0,sizeof(*s));s->attempted=true;s->io=io;s->stage="frame-address-plan";
  int r=a733_csi_raw10_frame_plan(1920,1080,physical,bytes,&s->plan);
  if(r)return s->error=r;
  s->started=io->now_ms(io->context);s->stage="frame-fabric-and-IOMMU-preflight";
  r=rd(s,0x020025e4,&s->mbus);
  /* No keyed shared fabric writes: require CSI/ISP gates already retained. */
  if(!r && (s->mbus&0x300)!=0x300)r=fault(s,-EACCES,0x020025e4,0x300,0x300,s->mbus);
  if(!r)r=rd(s,settings[0].a,&s->original[0]);
  if(r)return s->error=fault(s,r,s->last_address,0,0,0);
  s->owned=true;s->count=s->changed=1;
  s->address[0]=settings[0].a;s->mask[0]=settings[0].m;
  r=update(s,settings[0].a,7,7);
  if(!r)r=rd(s,0x03900000,&s->iommu_version);
  if(!r)r=rd(s,0x03900014,&s->iommu_enable);
  if(!r)r=rd(s,0x03900604,&s->iommu_bypass);
  /* Version v16 at this board; reject a different/absent block. The CSI
   * master is id1 in instance0, NOT the VIP2-private MMU or ISP master2. */
  if(!r && (s->iommu_version&0xffff)!=0x16)r=fault(s,-ENODEV,0x03900000,0xffff,0x16,s->iommu_version);
  if(!r && (s->iommu_enable&~1u))r=fault(s,-EPROTO,0x03900014,~1u,0,s->iommu_enable);
  if(!r && (s->iommu_bypass&~1u))r=fault(s,-EPROTO,0x03900604,~1u,0,s->iommu_bypass);
  if(!r && (s->iommu_enable&1) && !(s->iommu_bypass&1))r=fault(s,-ENOTSUP,0x03900604,1,1,s->iommu_bypass);
  s->stage="frame-exclusive-snapshot";
  /* Only CSI clocks are accessed before downstream register snapshots. */
  for(unsigned i=1;!r && i<entries();++i)
    {
      s->address[i]=settings[i].a;s->mask[i]=settings[i].m;
      if(i<FIRST_DMA)
        {
          r=rd(s,settings[i].a,&s->original[i]);
          if(!r && ((i==1 && (s->original[i]&0x80000000)) ||
             (i>=2 && i<=4 && (s->original[i]&settings[i].m))))r=-EBUSY;
          if(!r)
            {
              s->count=i+1;s->changed=i+1;
              uint32_t value=settings[i].v;
              if(i==1)value&=~0x80000000u;
              r=update(s,settings[i].a,settings[i].m,value);
              if(!r && i==1)r=update(s,settings[i].a,0x80000000,0x80000000);
              if(!r && i==1)r=pause_ms(s);
            }
        }
      else
        {r=rd(s,settings[i].a,&s->original[i]);if(!r)s->count=i+1;}
    }
  if(!r && ((s->original[FIRST_DMA]&1) || (s->original[DMA_ENABLE]&0x183) ||
            (s->original[PARSER_ENABLE]&1) || s->original[PARSER_CAPTURE] ||
            s->original[DMA_TOP_IRQ] || s->original[DMA_IRQ] || s->original[PARSER_IRQ]))r=-EBUSY;
  s->stage="frame-configure-parser1-RAW10-BK0";
  for(unsigned i=FIRST_DMA;!r && i<entries();++i)
    {
      s->changed=i+1;
      uint32_t value=settings[i].v;
      if(i>=ADDRESS_FIRST && i<=ADDRESS_LAST)value=s->plan.address_word;
      r=update(s,settings[i].a,settings[i].m,value);
      if(!r && i==PARSER_ENABLE)
        {r=update(s,P,1,0);if(!r)r=update(s,P,1,1);}
    }
  if(!r)r=rd(s,D,&s->sizing_active);
  if(!r)r=rd(s,D+0x238,&s->length_active);
  if(!r)r=rd(s,D+0x23c,&s->flip_active);
  if(!r)r=wr(s,D+0x254,VALID);
  if(!r)r=wr(s,P+0x44,7);
  uint32_t status=0;
  if(!r)r=rd(s,D+0x254,&status);
  if(!r && (status&VALID))r=-ESTALE;
  if(!r){s->armed=true;r=update(s,D+0x200,1,1);}
  if(!r)r=update(s,P+0xc,3,1); /* single frame at next source frame start */
  s->ready=!r;s->error=fault(s,r,s->last_address,0,0,0);
  s->stage=r?"frame-prepare-failed":"frame-armed-not-yet-captured";
  return r;
}
int a733_csi_capture_check(void *p)
{
  struct a733_csi_capture *s=p;if(!s || !s->ready || !s->owned)return -EACCES;
  uint32_t enable=0,bypass=0;int r=rd(s,0x03900014,&enable);
  if(!r)r=rd(s,0x03900604,&bypass);
  if(r)return r;
  if(enable!=s->iommu_enable)
    return fault(s,-EACCES,0x03900014,0xffffffff,s->iommu_enable,enable);
  if(bypass!=s->iommu_bypass)
    return fault(s,-EACCES,0x03900604,0xffffffff,s->iommu_bypass,bypass);
  for(unsigned i=FIRST_DMA;i<entries();++i)
    {
      uint32_t v,expected=settings[i].v;
      if(i>=ADDRESS_FIRST && i<=ADDRESS_LAST)expected=s->plan.address_word;
      if(i==DMA_ENABLE)expected|=1;
      if(i==PARSER_CAPTURE)expected=1;
      r=rd(s,settings[i].a,&v);if(r)return r;
      if((v&settings[i].m)!=(expected&settings[i].m))
        return fault(s,-EACCES,settings[i].a,settings[i].m,expected,v);
    }
  return 0;
}
int a733_csi_capture_wait(struct a733_csi_capture *s)
{
  if(!s || !s->ready)return -EACCES;
  int r=0;s->stage="frame-wait-real-DMA-completion";
  for(unsigned n=0;n<1000;++n)
    {
      uint32_t v=0;r=rd(s,D+0x254,&v);s->status|=v&VALID;
      if(r || (v&ERRORS))
        {if(!r)r=fault(s,-EBADMSG,D+0x254,ERRORS,0,v);break;}
      if(v&2){s->completed=true;break;}
      r=pause_ms(s);if(r)break;
    }
  if(!r && !s->completed)r=-ETIMEDOUT;
  if(!r)r=rd(s,P+0x44,&s->parser_status);
  if(!r)r=rd(s,P+0x3c,&s->parser_xy);
  if(!r && (s->parser_status&4))r=fault(s,-EBADMSG,P+0x44,4,0,s->parser_status);
  s->elapsed=s->io->now_ms(s->io->context)-s->started;
  s->error=fault(s,r,s->last_address,0,0,0);return r;
}
int a733_csi_capture_finish(struct a733_csi_capture *s)
{
  if(!s || !s->io)return -EINVAL;
  if(s->cleanup_error)return s->cleanup_error;
  if(!s->owned)return 0;
  s->ready=false;s->cleaning=true;s->cleanup_started=s->io->now_ms(s->io->context);int r=0;
  if(s->armed)
    {
      r=update(s,P+0xc,0x3f3f3f3f,0);
      if(!r)r=update(s,P,1,0);
      if(!r)r=update(s,D+0x200,1,0);
      bool drained=false;
      for(unsigned i=0;!r && i<100;++i)
        {
          uint32_t active=0;r=rd(s,D+8,&s->fifo);
          if(!r)r=rd(s,D+0x24c,&active);
          if(!r && (s->fifo&0x3fff)==((s->fifo>>16)&0x3fff) && !(active&1))
            {drained=true;break;}
          if(!r)r=pause_ms(s);
        }
      if(!r && !drained)r=fault(s,-ETIMEDOUT,D+8,0,0,s->fifo);
      /* Frame-done is not the end of error observation. After proven drain,
       * capture any late bus/parser error and mapping drift BEFORE gating.
       * A data rejection still restores known-stopped resources; an uncertain
       * read/restore error retains ownership exactly as before. */
      if(!r)r=rd(s,D+0x254,&s->final_status);
      if(!r)r=rd(s,P+0x44,&s->final_parser);
      if(!r)r=rd(s,0x03900014,&s->final_enable);
      if(!r)r=rd(s,0x03900604,&s->final_bypass);
      s->status|=s->final_status&VALID;s->parser_status|=s->final_parser;
      struct a733_mipi_fault late={0};
      if(s->final_status&ERRORS)late=(struct a733_mipi_fault){D+0x254,ERRORS,0,s->final_status,-EBADMSG};
      else if(s->final_parser&4)late=(struct a733_mipi_fault){P+0x44,4,0,s->final_parser,-EBADMSG};
      else if(!r && s->final_enable!=s->iommu_enable)
        late=(struct a733_mipi_fault){0x03900014,0xffffffff,s->iommu_enable,s->final_enable,-EACCES};
      else if(!r && s->final_bypass!=s->iommu_bypass)
        late=(struct a733_mipi_fault){0x03900604,0xffffffff,s->iommu_bypass,s->final_bypass,-EACCES};
      if(late.error && !s->error){s->error=late.error;if(!s->first_fault.error)s->first_fault=late;}
    }
  /* On uncertain drain, retain every clock/address/buffer until reboot. */
  for(unsigned n=s->changed;!r && n;--n)
    {
      r=update(s,s->address[n-1],s->mask[n-1],s->original[n-1]);
      if(!r && n-1==FIRST_DMA)
        {
          /* Restore source geometry/format before TOP's original mode.
           * Observe derived views AFTER that; do not write their snapshots.
           * Software latches were never touched, so mixed original modes
           * recover without guessing a byte/word unit for automatic reads. */
          r=rd(s,D,&s->sizing_restored);
          if(!r)r=rd(s,D+0x238,&s->length_restored);
          if(!r)r=rd(s,D+0x23c,&s->flip_restored);
        }
    }
  s->cleaning=false;s->cleanup_error=r;
  if(!r){s->owned=false;s->armed=false;s->changed=0;}
  s->stage=r?"frame-stop-restore-failed-reboot-required":
    s->error?"frame-stopped-with-data-error":s->completed?
      "frame-DMA-completed-stopped-buffer-not-yet-validated":"frame-stopped-without-completion";
  return r;
}

int a733_csi_capture_pause(struct a733_csi_capture *s)
{
  if(!s || !s->owned || !s->armed || !s->completed || s->error || s->cleanup_error)return -EACCES;
  s->ready=false;s->cleaning=true;s->cleanup_started=s->io->now_ms(s->io->context);
  int r=update(s,P+0xc,0x3f3f3f3f,0);
  if(!r)r=update(s,P,1,0);
  if(!r)r=update(s,D+0x200,1,0);
  bool drained=false;
  for(unsigned i=0;!r && i<100;++i)
    {
      uint32_t active=0;r=rd(s,D+8,&s->fifo);
      if(!r)r=rd(s,D+0x24c,&active);
      if(!r && (s->fifo&0x3fff)==((s->fifo>>16)&0x3fff) && !(active&1)){drained=true;break;}
      if(!r)r=pause_ms(s);
    }
  if(!r && !drained)r=fault(s,-ETIMEDOUT,D+8,0,0,s->fifo);
  if(!r)r=rd(s,D+0x254,&s->final_status);
  if(!r)r=rd(s,P+0x44,&s->final_parser);
  if(!r)r=rd(s,0x03900014,&s->final_enable);
  if(!r)r=rd(s,0x03900604,&s->final_bypass);
  if(!r && (s->final_enable!=s->iommu_enable || s->final_bypass!=s->iommu_bypass))r=-EACCES;
  s->status|=s->final_status&VALID;s->parser_status|=s->final_parser;
  if(!r && ((s->final_status&ERRORS) || (s->final_parser&4)))
    {
      /* A rejected frame is NOT a failed drain. Keep its first raw status,
       * but do not turn a known-stopped data error into cleanup quarantine. */
      s->cleaning=false;
      s->error=s->final_status&ERRORS ?
        fault(s,-EBADMSG,D+0x254,ERRORS,0,s->final_status) :
        fault(s,-EBADMSG,P+0x44,4,0,s->final_parser);
    }
  s->cleaning=false;
  if(r)
    {
      s->cleaning=true;s->cleanup_error=fault(s,r,s->last_address,0,0,0);
      s->cleaning=false;return r;
    }
  s->armed=false;s->paused=true;s->stage="sensor-on-frame-DMA-paused-resources-retained";
  return s->error;
}
int a733_csi_capture_rearm(struct a733_csi_capture *s)
{
  if(!s || !s->owned || s->armed || !s->paused || !s->completed || s->error || s->cleanup_error)return -EACCES;
  s->started=s->io->now_ms(s->io->context);s->polls=0;
  s->status=s->parser_status=s->final_status=s->final_parser=0;s->completed=false;
  uint32_t active=0;int r=rd(s,D+8,&s->fifo);
  if(!r)r=rd(s,D+0x24c,&active);
  if(!r && ((s->fifo&0x3fff)!=((s->fifo>>16)&0x3fff) || (active&1)))r=-EBUSY;
  if(!r)r=rd(s,0x03900014,&s->final_enable);
  if(!r)r=rd(s,0x03900604,&s->final_bypass);
  if(!r && (s->final_enable!=s->iommu_enable || s->final_bypass!=s->iommu_bypass))r=-EACCES;
  if(!r)r=rd(s,D+0x220,&active);
  if(!r && active!=s->plan.address_word)r=-EACCES;
  /* The register retaining an old address does NOT prove next-frame address
   * readiness. Re-submit all three owned address ports while parser/DMA are
   * stopped, as the vendor queue path does for the next buffer. Read back
   * each write; never enable CAP after an uncertain/partial address update.
   * No FIFO-mode change, continuous writes, reset, retry or error masking. */
  for(unsigned i=ADDRESS_FIRST;!r && i<=ADDRESS_LAST;++i)
    r=update(s,settings[i].a,settings[i].m,s->plan.address_word);
  if(!r)r=wr(s,D+0x254,VALID);
  if(!r)r=wr(s,P+0x44,7);
  if(!r)r=rd(s,D+0x254,&active);
  if(!r && (active&VALID))r=-ESTALE;
  if(!r){s->armed=true;r=update(s,D+0x200,1,1);}
  if(!r)r=update(s,P,1,1);
  if(!r)r=update(s,P+0xc,3,1); /* gated single capture, NOT cyclic DMA */
  s->ready=!r;s->paused=false;s->error=fault(s,r,s->last_address,0,0,0);
  s->stage=r?"sensor-on-frame-rearm-failed":"sensor-on-next-single-frame-armed";
  return r;
}
