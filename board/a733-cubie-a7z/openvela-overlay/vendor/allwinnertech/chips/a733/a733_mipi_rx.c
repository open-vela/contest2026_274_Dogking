/* SPDX-License-Identifier: Apache-2.0 */
/* Independent bounded transaction engine. Register facts cross-checked
 * against the local sun60iw2 DTS/CCU/combo CSI BSP; see v195 provenance.
 * Not a transplant of Linux lifecycle/code; no vendor register header linked.
 */
#include "a733_mipi_rx.h"
#include "a733_imx214_cold.h"
#include <errno.h>
#include <string.h>
#define MODULE 0x02003840u
#define BUS    0x02003844u
#define CCU    0x05800000u
#define TOP    0x05800800u
#define PHYTOP 0x05810000u
#define PHY    0x05810200u
#define PORT   0x05811400u
#define STATUS (PORT+0x118)
#define HEADER (PORT+0x11c)
#define ERROR_BITS 0x1f80u
struct setting { uint32_t address,mask,value; };
/* Index order is lifetime order; reverse restoration keeps bus accessible.
 * Parent 2 /2: nominal 240MHz from the already configured PERI0 /5 output.
 * No shared PLL/MBUS/ISP clocks, parser or DMA registers are written.
 */
static const struct setting plan[] = {
  {BUS,0x10001,0x10001}, {MODULE,0x8700001f,0x82000001},
  {CCU,0x80000003,0x80000003}, {CCU+4,0x100,0x100},
  {TOP,1,1}, {PHYTOP,0x60000f0f,0xb0f},
  {PHYTOP+4,0x000001f7,0x000000a7},
  {PHY,0x340713f1,0x040411f0},
  {PHY+8,0x1010,0x1000}, {PHY+12,0x01f00000,0x01300000},
  {PHY+0x20,0x00ff003f,0}, {PHY+0x24,0x00ff003f,0},
  {PHY+0x28,0x00ff030f,0x00280300},
  {PHY+0x2c,0x00013f04,0x1f00}, {PHY+0x18,0x01f00000,0x00700000},
  {PHY+0x34,1,1}, {PORT,0x80030f31,0x80000400},
  {PORT+4,0xffff,0x7654}, {PORT+0x100,0x000300ff,1},
  {PORT+0x108,0xff,0x2b}, {PORT+0x110,0xe00f000f,0x000e000f},
  {PORT+0x114,0x1fff,0}, /* polling; NEVER install/enable an IRQ */
  /* A733 HW type4/type10: PIO +0x80 + bankK(10)*0x80.
   * GPIO mux2=MCSIB, 10mA drive encoding0, other PK pins untouched. */
  {0x02000584,0xffffff00,0x22222200}, {0x02000588,0xffff,0x2222},
  {0x020005a4,0xffffff00,0}, {0x020005a8,0xffff,0}
};
_Static_assert(sizeof(plan)/sizeof(plan[0])<=28,"snapshot capacity");
static unsigned count(void){return sizeof(plan)/sizeof(plan[0]);}
uint32_t a733_mipi_rx_volatile_mask(uint32_t a)
{
  if(a==PHYTOP+4)return 0x001f0008; /* calibration result and flag */
  if(a==PHY+8)return 0x30000000; /* offset calibration flags */
  if(a==PHY+0x34)return 0xffff0000; /* live frequency count */
  return 0;
}
static int fault(struct a733_mipi_rx *s,int error,uint32_t a,
                 uint32_t mask,uint32_t expected,uint32_t observed)
{
  struct a733_mipi_fault *f=s->cleaning?&s->cleanup_fault:&s->first_fault;
  if(error && !f->error)
    *f=(struct a733_mipi_fault){a,mask,expected,observed,error};
  return error;
}
uint32_t a733_mipi_rx_write_mask(uint32_t a)
{
  for(unsigned i=0;i<count();++i)if(plan[i].address==a)return plan[i].mask;
  return a==STATUS ? 0x1fff : 0;
}
bool a733_mipi_rx_readable(uint32_t a)
{
  if(a733_mipi_rx_write_mask(a) || a==HEADER || a==PHY+0xf0 ||
     a==0x02002000 || a==0x020020a0)return true;
  /* Unused blocks must have no enabled clock/client before global writes. */
  return a==CCU+8 || a==CCU+12 || a==CCU+16 ||
    a==0x05810100 || a==0x05810300 ||
    a==0x05811000 || a==0x05811800;
}
static int bounded(struct a733_mipi_rx *s)
{
  s->elapsed=s->io->now_ms(s->io->context)-s->started;
  return s->elapsed>=1500 || ++s->polls>200000 ? -ETIMEDOUT : 0;
}
static int rd(struct a733_mipi_rx *s,uint32_t a,uint32_t *v)
{
  if(!a733_mipi_rx_readable(a))return fault(s,-EACCES,a,0,0,0);
  if(!s->cleaning && bounded(s))return fault(s,-ETIMEDOUT,a,0,0,0);
  s->last_address=a;++s->reads;
  uint32_t start=s->io->now_ms(s->io->context);
  int r=s->io->read32(s->io->context,a,v,10);
  if(r>0)r=-EPROTO;
  if(!r && s->io->now_ms(s->io->context)-start>10)r=-ETIMEDOUT;
  return fault(s,r,a,0,0,0);
}
static int wr(struct a733_mipi_rx *s,uint32_t a,uint32_t v)
{
  if(!a733_mipi_rx_write_mask(a))return fault(s,-EACCES,a,0,0,0);
  if(!s->cleaning && bounded(s))return fault(s,-ETIMEDOUT,a,0,0,0);
  s->last_address=a;++s->writes;
  uint32_t start=s->io->now_ms(s->io->context);
  int r=s->io->write32(s->io->context,a,v,10);
  if(r>0)r=-EPROTO;
  if(!r && s->io->now_ms(s->io->context)-start>10)r=-ETIMEDOUT;
  return fault(s,r,a,0,0,0);
}
static int update(struct a733_mipi_rx *s,uint32_t a,uint32_t mask,uint32_t v)
{
  uint32_t before,after;int r=rd(s,a,&before);
  if(!r)r=wr(s,a,(before&~(mask|a733_mipi_rx_volatile_mask(a)))|(v&mask));
  if(!r)r=rd(s,a,&after);
  if(!r && (after&mask)!=(v&mask))r=fault(s,-EIO,a,mask,v,after);
  return r;
}
static int pause(struct a733_mipi_rx *s,unsigned ms)
{
  uint32_t before=s->io->now_ms(s->io->context);
  int r=s->io->delay_ms(s->io->context,ms);
  uint32_t elapsed=s->io->now_ms(s->io->context)-before;
  if(r>0)r=-EPROTO;
  if(!r && (elapsed<ms || elapsed>ms+20))r=-ETIMEDOUT;
  return fault(s,r,s->last_address,0,0,0);
}
int a733_mipi_rx_finish(struct a733_mipi_rx *s)
{
  if(!s || !s->io)return -EINVAL;
  if(s->cleanup_error)return s->cleanup_error;
  if(!s->owned)return 0;
  s->ready=false;s->cleaning=true;
  /* No DMA was started and no DDR target exists. Disable sink before PHY;
   * even a sensor STOP failure cannot cause writes to system memory.
   */
  int r=0;
  if(s->changed>16)r=update(s,PORT,1,0);
  if(!r && s->changed>7)r=update(s,PHY,1,0);
  for(unsigned n=s->changed;!r && n;--n)
    r=update(s,plan[n-1].address,plan[n-1].mask,s->original[n-1]);
  s->cleaning=false;s->cleanup_error=r;
  if(!r){s->owned=false;s->changed=0;}
  else s->stage="receiver-restore-failed-reboot-required";
  return r;
}
int a733_mipi_rx_prepare(struct a733_mipi_rx *s,const struct a733_imx214_cold_io *io)
{
  if(!s || !io || !io->read32 || !io->write32 || !io->now_ms || !io->delay_ms)return -EINVAL;
  if(s->attempted || s->owned)return -EALREADY;
  memset(s,0,sizeof(*s));s->attempted=true;s->io=io;
  s->started=io->now_ms(io->context);s->stage="receiver-clock-preflight";
  uint32_t v;int r=rd(s,0x02002000,&s->pll_ref);
  if(!r)r=rd(s,0x020020a0,&s->pll_peri);
  /* This release supports the observed factory profile only. Reject other
   * roots rather than guessing a rate or retuning shared clocks. */
  if(!r && (s->pll_ref!=0xf8675f00 || s->pll_peri!=0xff126310))r=-ERANGE;
  if(!r)r=rd(s,BUS,s->original);
  if(!r)r=rd(s,MODULE,s->original+1);
  if(!r && ((s->original[0]&0x10001) || (s->original[1]&0x80000000)))r=-EBUSY;
  if(r)return s->error=fault(s,r,s->last_address,0,0,0);
  s->owned=true;s->saved=2;s->changed=1;
  r=update(s,BUS,0x10001,1); /* gate, then release reset */
  if(!r)r=update(s,BUS,0x10001,0x10001);
  s->changed=2;
  if(!r)r=update(s,MODULE,0x8700001f,0x02000001);
  if(!r)r=update(s,MODULE,0x80000000,0x80000000);
  if(!r)r=pause(s,1);
  s->stage="receiver-exclusive-idle-check";
  for(unsigned i=2;!r && i<count();++i)
    {r=rd(s,plan[i].address,s->original+i);if(!r)s->saved=i+1;}
  if(!r && ((s->original[2]&0x80000000) || s->original[3] ||
            (s->original[4]&1) || (s->original[5]&0xf) ||
            (s->original[7]&3) || (s->original[16]&1)))r=-EBUSY;
  for(unsigned pin=10;!r && pin<=19;++pin)
    {
      unsigned word=pin<16?22:23,shift=(pin%8)*4;
      uint32_t mux=(s->original[word]>>shift)&15;
      if(mux!=0 && mux!=15)r=-EBUSY;
    }
  const uint32_t idle[]={CCU+8,CCU+12,CCU+16,0x05810100,0x05810300,
                          0x05811000,0x05811800};
  for(unsigned i=0;!r && i<sizeof(idle)/sizeof(idle[0]);++i)
    {r=rd(s,idle[i],&v);if(!r && (i<3 ? v!=0 : (v&3)!=0))r=-EBUSY;}
  s->stage="receiver-configure-MCSIB-4lane-RAW10-VC0";
  for(unsigned i=2;!r && i<count();++i)
    {
      s->changed=i+1;
      r=update(s,plan[i].address,plan[i].mask,plan[i].value);
      if(!r && i==6) /* Pulse calibration reset, preserve status/read-only flag. */
        {r=update(s,PHYTOP+4,4,0);if(!r)r=update(s,PHYTOP+4,4,4);}
    }
  if(!r)r=update(s,PHY,1,1);
  if(!r)r=pause(s,1);
  if(!r)r=wr(s,STATUS,0x1fff);
  if(!r)r=rd(s,STATUS,&v);
  if(!r && (v&0x1fff))r=-ESTALE; /* Never accept old packet status as live. */
  if(!r)r=update(s,PORT,1,1);
  s->ready=!r;s->error=fault(s,r,s->last_address,0,0,0);
  s->stage=r ? "receiver-prepare-failed" : "receiver-ready-NOT-a-frame";
  return r;
}
int a733_mipi_rx_check(void *p)
{
  struct a733_mipi_rx *s=p;
  if(!s || !s->ready || !s->owned || s->cleanup_error)return -EACCES;
  const unsigned indices[]={0,1,2,3,5,7,16,17,18,19,20,21,22,23,24,25};
  for(unsigned j=0;j<sizeof(indices)/sizeof(indices[0]);++j)
    {
      unsigned i=indices[j];uint32_t v;int r=rd(s,plan[i].address,&v);
      uint32_t expected=plan[i].value | ((i==7 || i==16) ? 1u : 0);
      if(r)return r;
      if((v&plan[i].mask)!=(expected&plan[i].mask))
        return fault(s,-EACCES,plan[i].address,plan[i].mask,expected,v);
    }
  return 0;
}
int a733_mipi_rx_snapshot(struct a733_mipi_rx *s)
{
  if(!s || !s->ready || !s->owned)return -EACCES;
  uint32_t status=0,header=0;int r=rd(s,STATUS,&status);
  if(!r)r=rd(s,HEADER,&header);
  if(r)return r;
  s->status|=status&0x1fff;s->errors|=status&ERROR_BITS;s->header=header;
  if(status&4)++s->fs;
  if(status&8)++s->fe;
  if((status&1) && (header&255)==0x2b)++s->raw_headers;
  if(status&0x1fff)r=wr(s,STATUS,status&0x1fff);
  if(!r && s->errors)r=fault(s,-EBADMSG,STATUS,ERROR_BITS,0,status);
  return r;
}
int a733_mipi_rx_observe(struct a733_mipi_rx *s)
{
  int r=a733_mipi_rx_check(s);if(r)return r;
  uint32_t start=s->io->now_ms(s->io->context);
  s->stage="receiver-observe-real-packets-NOT-a-frame";
  for(unsigned i=0;!r && i<500;++i)
    {
      r=a733_mipi_rx_snapshot(s);
      if(!r)r=pause(s,1);
      if(s->io->now_ms(s->io->context)-start>=500)break;
    }
  if(!r)r=rd(s,PHY+0xf0,&s->phy_debug);
  if(!r)r=rd(s,PHY+0x34,&s->phy_frequency);
  if(!r && (!s->raw_headers || !s->fs || !s->fe))r=-ENODATA;
  s->error=fault(s,r,s->last_address,0,0,0);
  s->elapsed=s->io->now_ms(s->io->context)-s->started;
  s->stage=r ? "receiver-no-valid-packet-sequence" : "MIPI-packets-observed-NOT-RAW-frame";
  return r;
}
