/* SPDX-License-Identifier: Apache-2.0 */
#include "mcu_link.hxx"
#include "uart_tts.hxx"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace aipet {
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t call_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {
  bool running=false,ready=false,quit=false,armed=false,urgent=false;
  bool pending=false,sent=false,done=false,seen=false;
  bool desktop=false,desktop_inhibit=false;
  uint16_t seq=0,ping_seq=0,job_seq=0;
  uint64_t pong_ms=0,arm_until=0,deadline=0;
  uint32_t uptime=0,config=0;
  uint8_t fault=255,flags=0,impl=0,status=255;
  int error=0,result=0;
  int pid=0;
  unsigned tx=0,rx=0,bad=0,done_events=0;
  unsigned edge_events=0,obstacle_events=0;
  uint64_t tof_ms=0;
  uint8_t tof_mask=0,detail=0;
  std::array<int,4> distances{{-1,-1,-1,-1}};
  std::array<int64_t,2> positions{{0,0}};
  McuFrame response;
  McuCommand command;
} link;
static uint64_t now_ms()
{
  timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
  return uint64_t(t.tv_sec)*1000+uint64_t(t.tv_nsec)/1000000;
}
static uint32_t little32(const std::vector<uint8_t> &p,size_t i)
{return uint32_t(p[i])|(uint32_t(p[i+1])<<8)|(uint32_t(p[i+2])<<16)|(uint32_t(p[i+3])<<24);}
static bool fresh(uint64_t now) {return link.seen && now-link.pong_ms<=750;}
// Zero is reserved for unsolicited events; commands wrap from 65535 to 1.
static uint16_t next_seq() {if(++link.seq==0)++link.seq;return link.seq;}
static int allowed(const McuCommand &c,uint64_t now)
{
  if(c.op==12 && c.payload.empty())return 0;
  if((c.op==6||c.op==8||c.op==9||c.op==10) && c.payload.empty())return 0;
  if(!fresh(now))return -ENOLINK;
  if(c.op==5 && (c.payload==std::vector<uint8_t>{0} || c.payload==std::vector<uint8_t>{0,0x5a}))return 0;
  if(c.op==7 && c.payload.size()==4 && (link.impl&16) && !link.armed &&
     !(little32(c.payload,0)&~0x7fu))return 0;
  if(c.op==4 && c.payload.size()==1 && c.payload[0]>=10 && (link.impl&8))return 0;
  if(c.op==3 && c.payload.size()>=5 && c.payload.size()<=200 &&
     c.payload[0]==0xfd && c.payload[1]==0 && c.payload[2]==c.payload.size()-3 &&
     c.payload[3]==1 && c.payload[4]==4 && (link.impl&4) && (link.config&2))return 0;
  if(!link.armed||now>=link.arm_until||link.fault)return -EACCES;
  McuCommand valid;
  if(c.op==1 && c.payload.size()==4 && (link.impl&2)) {
    unsigned steps=unsigned(c.payload[2])|(unsigned(c.payload[3])<<8);
    return mcu_move(c.payload[0],c.payload[1],steps,valid)&&mcu_same(c,valid)?0:-EINVAL;
  }
  if(c.op==2 && c.payload.size()==3 && (link.impl&1) && (link.config&1)) {
    int degrees=c.payload[0]<128?int(c.payload[0]):int(c.payload[0])-256;
    unsigned ms=unsigned(c.payload[1])|(unsigned(c.payload[2])<<8);
    return mcu_servo(degrees,ms,valid)&&mcu_same(c,valid)?0:-EINVAL;
  }
  return -ENOTSUP;
}
static int send_frame(int fd,uint16_t seq,const McuCommand &c)
{
  std::vector<uint8_t> raw;
  if(!mcu_encode({1,c.op,seq,c.payload},raw))return -EINVAL;
  size_t sent=0;uint64_t end=now_ms()+50;
  while(sent<raw.size()) {
    if(now_ms()>=end)return -ETIMEDOUT;
    ssize_t n=write(fd,raw.data()+sent,raw.size()-sent);
    if(n<0 && (errno==EAGAIN||errno==EINTR)) {usleep(1000);continue;}
    if(n<=0)return n<0?-errno:-EIO;
    sent+=size_t(n);
  }
  return 0;
}
static void receive(const McuFrame &f,uint64_t now)
{
  struct Inhibit {bool armed;~Inhibit(){if(armed&&!link.armed)link.desktop_inhibit=true;}} inhibit{link.armed};
  ++link.rx;
  if(f.type==3) {
    if(f.seq!=0)return; // broadcast events never acknowledge a command
    if(f.op==0x92 && f.payload.size()==9) {
      link.tof_mask=f.payload[0]&15;link.tof_ms=now;
      for(unsigned i=0;i<4;i++) {
        unsigned d=unsigned(f.payload[1+i*2])|(unsigned(f.payload[2+i*2])<<8);
        link.distances[i]=(link.tof_mask&(1u<<i))&&d!=65535?int(d):-1;
      }
    } else if((f.op==0x91&&f.payload.size()==3&&f.payload[0]<3)||
              (f.op==0x95&&f.payload.size()==2)) {
      if(f.op==0x91){++link.edge_events;link.fault=2;}
      else {++link.obstacle_events;link.fault=3;}
      link.armed=false;link.urgent=true;
      if(link.pending&&!link.done&&(link.command.op==1||link.command.op==2)) {
        link.done=true;link.result=-ECANCELED;
      }
    } else if(f.op==0x93 && f.payload.size()==1) {
      link.fault=f.payload[0];link.armed=false;link.urgent=true;
      if(link.pending&&!link.done&&(link.command.op==1||link.command.op==2)) {
        link.done=true;link.result=-ECANCELED;
      }
    } else if(f.op==0x94 && f.payload.size()==5 && f.payload[0]<2) {
      ++link.done_events; // Diagnostic only: not correlated enough to promise completion.
      uint32_t value=little32(f.payload,1);
      link.positions[f.payload[0]]=value<=0x7fffffffu?int64_t(value):int64_t(value)-4294967296LL;
    } else if(f.op==0x96 && f.payload.size()==4) {
      link.config=little32(f.payload,0);
      link.armed=false;link.urgent=true;
      if(link.pending&&!link.done&&(link.command.op==1||link.command.op==2)) {
        link.done=true;link.result=-ECANCELED;
      }
    }
    return;
  }
  if(f.type!=2)return;
  bool job=link.pending&&link.sent&&!link.done&&now<link.deadline&&f.seq==link.job_seq;
  if(f.op==0x86 && f.payload.size()==11 && (f.seq==link.ping_seq || (job&&link.command.op==6))) {
    uint32_t uptime=little32(f.payload,0),config=little32(f.payload,5);
    if(link.seen && (uptime<link.uptime || config!=link.config)) {
      link.armed=false;link.urgent=true;
      if(link.pending&&!link.done&&(link.command.op==1||link.command.op==2)) {
        link.done=true;link.result=-ECANCELED;
      }
    }
    link.seen=true;link.pong_ms=now;link.uptime=uptime;link.config=config;
    link.fault=f.payload[4];link.flags=f.payload[9];link.impl=f.payload[10];
    if(link.fault) {
      link.armed=false;
      if(link.pending&&!link.done&&(link.command.op==1||link.command.op==2)) {
        link.done=true;link.result=-ECANCELED;link.urgent=true;
      }
    }
    if(job&&link.command.op==6){link.done=true;link.result=0;link.status=0;}
  } else if(job && ((link.command.op==8&&f.op==0x87&&f.payload.size()==4)||
                    (link.command.op==9&&f.op==9&&f.payload.size()==24)||
                    (link.command.op==10&&f.op==10&&f.payload.size()==32))) {
    link.response=f;link.status=0;link.result=0;link.done=true;
    if(f.op==0x87 && little32(f.payload,0)!=link.config) {
      link.config=little32(f.payload,0);link.armed=false;link.urgent=true;
    }
    if(f.op==10) {
      link.fault=f.payload[24];
      if(link.fault || f.payload[25] || f.payload[26]!=0){link.armed=false;link.urgent=true;}
    }
    if(f.op==9)link.fault=f.payload[23];
    if(f.op==9 && (f.payload[23] || (f.payload[10]&2) || (f.payload[21]&2))) {
      link.armed=false;link.urgent=true;
    }
  } else if(job && f.op==0x81 &&
            ((link.command.op==1&&f.payload.size()==2) ||
             ((link.command.op==2||link.command.op==3||link.command.op==4||link.command.op==5||
               link.command.op==7||link.command.op==12)&&f.payload.size()==1))) {
    link.response=f;link.status=f.payload[0];link.done=true;link.result=link.status==0?0:-EREMOTEIO;
    if(link.command.op==3 && link.status==2)link.result=0; // queued with backpressure, NOT spoken
    if(link.result){link.armed=false;link.urgent=true;}
  } else if(job && f.op==0x82 && f.payload.size()==2) {
    link.detail=f.payload[1];
    link.status=f.payload[0];link.done=true;link.result=-EREMOTEIO;
    link.armed=false;link.urgent=true;
  }
}
static void *worker(void *)
{
  int fd=open("/dev/ttyS4",O_RDWR|O_NOCTTY|O_NONBLOCK),error=fd<0?-errno:0;
  termios t{};
  if(!error && tcgetattr(fd,&t))error=-errno;
  if(!error) {
    t.c_iflag=0;t.c_oflag=0;t.c_lflag=0;t.c_cflag=CS8|CREAD|CLOCAL;
    t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
    if(cfsetispeed(&t,B115200)||cfsetospeed(&t,B115200)||tcsetattr(fd,TCSANOW,&t))error=-errno;
  }
  pthread_mutex_lock(&lock);link.ready=!error;link.error=error;pthread_mutex_unlock(&lock);
  McuParser parser;uint64_t ping_at=0;
  while(!error) {
    uint64_t now=now_ms();
    uint8_t bytes[256];ssize_t n=read(fd,bytes,sizeof(bytes));
    if(n<0 && errno!=EAGAIN && errno!=EINTR){error=-errno;break;}
    pthread_mutex_lock(&lock);
    for(ssize_t i=0;i<n;i++){McuFrame f;if(parser.feed(bytes[i],now,f))receive(f,now);}
    link.bad=parser.bad;
    if(link.quit){pthread_mutex_unlock(&lock);break;}
    if(link.armed && (!fresh(now)||now>=link.arm_until||link.fault)) {
      link.armed=false;link.desktop_inhibit=true;link.urgent=true;link.error=-ENOLINK;
    }
    McuCommand command;uint16_t seq=0;bool send=false;
    if(link.urgent) {link.urgent=false;command={12,{}};seq=next_seq();send=true;}
    else if(link.pending&&!link.sent&&!link.done) {
      int gate=allowed(link.command,now);
      if(gate||now>=link.deadline) {link.done=true;link.result=gate?gate:-ETIMEDOUT;}
      else {command=link.command;seq=next_seq();link.job_seq=seq;link.sent=true;send=true;}
    } else if(now>=ping_at) {
      command={6,{}};seq=next_seq();link.ping_seq=seq;ping_at=now+250;send=true;
    }
    if(link.pending&&!link.done&&now>=link.deadline) {
      link.done=true;link.result=-ETIMEDOUT;link.armed=false;link.desktop_inhibit=true;link.urgent=true;
    }
    pthread_mutex_unlock(&lock);
    if(send) {
      error=send_frame(fd,seq,command);
      pthread_mutex_lock(&lock);if(!error)++link.tx;pthread_mutex_unlock(&lock);
    }
    usleep(2000);
  }
  // Best effort STOP on explicit disconnect/I/O failure; never claim an ACK here.
  if(fd>=0){
    pthread_mutex_lock(&lock);uint16_t seq=next_seq();pthread_mutex_unlock(&lock);
    (void)send_frame(fd,seq,{12,{}});close(fd);
  }
  pthread_mutex_lock(&lock);
  link.ready=false;link.running=false;link.armed=false;link.desktop_inhibit=true;link.error=error;
  if(link.pending&&!link.done){link.done=true;link.result=error?error:-ECANCELED;}
  pthread_mutex_unlock(&lock);return nullptr;
}
static int start()
{
  pthread_mutex_lock(&lock);
  if(link.running){pthread_mutex_unlock(&lock);return 0;}
  link.running=true;link.quit=false;link.ready=false;link.armed=false;link.seen=false;
  link.error=0;link.urgent=false;link.pong_ms=0;
  link.tof_ms=0;link.tof_mask=0;link.distances={{-1,-1,-1,-1}};
#if defined(__NuttX__) || defined(AIPET_MCU_TEST_SPAWN)
  // A pthread owned by this short-lived NSH command would be killed when its
  // main task exits. Spawn a separate persistent builtin task instead.
  pid_t pid=-1;
  char name[]="aipet",argument[]="--mcu-worker";
  char *args[]={name,argument,nullptr};
  int rc=0;
#ifdef __NuttX__
  posix_spawn_file_actions_t files;
  rc=posix_spawn_file_actions_init(&files);bool initialized=!rc;
  if(!rc)rc=posix_spawn_file_actions_addopen(&files,0,"/dev/null",O_RDONLY,0);
  if(!rc)rc=posix_spawn_file_actions_addopen(&files,1,"/dev/a7zconsole",O_WRONLY,0);
  if(!rc)rc=posix_spawn_file_actions_addopen(&files,2,"/dev/a7zconsole",O_WRONLY,0);
  if(!rc)rc=posix_spawn(&pid,name,&files,nullptr,args,nullptr);
  if(initialized)(void)posix_spawn_file_actions_destroy(&files);
#else
  rc=posix_spawn(&pid,name,nullptr,nullptr,args,nullptr);
#endif
  if(!rc)link.pid=int(pid);
#else
  pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,16384);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  pthread_t thread;int rc=pthread_create(&thread,&attr,worker,nullptr);
  pthread_attr_destroy(&attr);
#endif
  if(rc){link.running=false;link.error=-rc;}
  pthread_mutex_unlock(&lock);return rc?-rc:0;
}
int mcu_worker_main()
{
  pthread_mutex_lock(&lock);
  bool owner=link.running&&link.pid==getpid();
  pthread_mutex_unlock(&lock);
  if(!owner)return 1;
  worker(nullptr);return 0;
}
int mcu_request(const McuCommand &command,std::string &result)
{
  result.clear();
  if(command.op==12){pthread_mutex_lock(&lock);link.armed=false;link.urgent=true;pthread_mutex_unlock(&lock);}
  if(pthread_mutex_trylock(&call_lock)) {result="链路正忙；停止请求已优先排队（若本次为停止），尚未确认。";return -EBUSY;}
  pthread_mutex_lock(&lock);
  int rc=!link.ready?-ENODEV:allowed(command,now_ms());
  if(!rc) {
    link.command=command;link.pending=true;link.sent=false;link.done=false;link.status=255;
    link.result=0;link.detail=0;link.response={};link.deadline=now_ms()+600;
  }
  pthread_mutex_unlock(&lock);
  if(!rc) {
    uint64_t deadline=now_ms()+1000;
    for(;;) {
      pthread_mutex_lock(&lock);
      bool done=link.done;
      if(done)rc=link.result;
      if(done || now_ms()>=deadline) {
        if(!done){rc=-ETIMEDOUT;link.armed=false;link.desktop_inhibit=true;link.urgent=true;}
        link.pending=false;pthread_mutex_unlock(&lock);break;
      }
      pthread_mutex_unlock(&lock);usleep(2000);
    }
  }
  if(!rc) {
    if(command.op==6) {
      char text[128];pthread_mutex_lock(&lock);
      std::snprintf(text,sizeof(text),"下位机已响应，故障码=%u，能力=0x%02x，运动解锁=%u。",link.fault,link.impl,link.armed);
      pthread_mutex_unlock(&lock);result=text;
    } else if(command.op==8 || command.op==9 || command.op==10) {
      pthread_mutex_lock(&lock);auto p=link.response.payload;pthread_mutex_unlock(&lock);
      char text[480]{};
      auto s32=[&](size_t i){uint32_t v=little32(p,i);return v<=0x7fffffffu?int64_t(v):int64_t(v)-4294967296LL;};
      if(command.op==8)std::snprintf(text,sizeof(text),"下位机配置=0x%08x；读取不写 Flash。",unsigned(little32(p,0)));
      if(command.op==9)std::snprintf(text,sizeof(text),"电机脉冲状态：左位置=%lld 剩余=%lld 频率=%u 标志=%02x；右位置=%lld 剩余=%lld 频率=%u 标志=%02x；热跳闸=%u 故障=%u。不是编码器实测。",
        (long long)s32(0),(long long)s32(4),unsigned(p[8])|(unsigned(p[9])<<8),p[10],
        (long long)s32(11),(long long)s32(15),unsigned(p[19])|(unsigned(p[20])<<8),p[21],p[22],p[23]);
      if(command.op==10)std::snprintf(text,sizeof(text),"链路状态：好帧=%u 坏帧=%u 字节=%u 溢出=%u 帧龄=%u ms 故障=%u 失联=%u owner=%u A7Z=%u 蓝牙=%u 蓝牙初始化=%u。",
        unsigned(little32(p,4)),unsigned(little32(p,8)),unsigned(little32(p,12)),unsigned(little32(p,16)),unsigned(little32(p,20)),p[24],p[25],p[26],p[27],p[28],p[29]);
      result=text;
    } else if(command.op==3)result="下位机已将语音帧入队；不是播放完成确认，积压时请降速。";
    else if(command.op==7)result="配置写入已确认；运动仍未解锁，请重新查询配置和故障。";
    else if(command.op==4)result="TOF轮询间隔设置已确认；没有更改传感器启用集。";
    else if(command.op==12)result="下位机已确认停止命令；运动权限已关闭。";
    else if(command.op==1) {
      pthread_mutex_lock(&lock);unsigned distance=link.response.payload[1];pthread_mutex_unlock(&lock);
      char text[240];std::snprintf(text,sizeof(text),"下位机已接收运动命令，预测距离字段=%u%s；这不是动作完成确认，也不是实际位移测量。",distance,distance==255?"（饱和，超过25.4毫米）":"（单位0.1毫米）");result=text;
    } else result="下位机已确认接收动作命令；这不是动作完成确认。";
  } else if(rc==-EACCES)result="动作未发送，请检查下位机故障或重新开启桌宠控制。";
  else if(rc==-ENOTSUP&&command.op==2)result="舵机动作未发送：请检查舵机能力和配置。向左看、向右看需要实际安装舵机；底盘转向请说左转、右转。";
  else if(rc==-EREMOTEIO) {
    pthread_mutex_lock(&lock);unsigned status=link.status,detail=link.detail;pthread_mutex_unlock(&lock);
    char text[360];std::snprintf(text,sizeof(text),"下位机拒绝或钳位：status=0x%02x detail=0x%02x。仲裁=09，功能关闭=06，忙=02；03/01须查电机cooling，不能把detail直接当故障；Flash失败可能已在RAM生效。不会自动重发。",status,detail);result=text;
  }
  else result="下位机通信未确认，动作结果未知；不会自动重发运动命令。";
  pthread_mutex_unlock(&call_lock);return rc;
}
static bool number(const char *s,int low,int high,int &v)
{
  if(!s||!*s)return false;
  bool minus=*s=='-';if(minus)++s;
  if(!*s)return false;
  int n=0;
  for(;*s;s++){if(*s<'0'||*s>'9'||n>10000)return false;n=n*10+(*s-'0');}
  v=minus?-n:n;return v>=low&&v<=high;
}
void mcu_desktop_session(bool enabled) {
  pthread_mutex_lock(&lock);link.desktop=enabled;
  if(enabled)link.desktop_inhibit=false; // Explicit service start/user consent, not a reconnect callback.
  else {link.armed=false;link.urgent=true;}
  pthread_mutex_unlock(&lock);
  std::printf("mcu-desktop-v225: enabled=%u; bounded HUMAN commands may authorize once per command; fault/loss/NAK inhibit, no clear/retry\n",enabled);
}
int mcu_control(int argc,char **argv)
{
  int rc=0;McuCommand c;std::string text;
  if(argc==1&&!std::strcmp(argv[0],"start"))rc=start();
  else if(argc==0||(argc==1&&!std::strcmp(argv[0],"status"))) {
    pthread_mutex_lock(&lock);
    std::printf("mcu: running=%u ready=%u online=%u armed=%u baud=115200 device=/dev/ttyS4 fault=%u cfg=%08x impl=%02x error=%d pid=%d\n"
                "mcu: tx=%u rx=%u crc-drops=%u motion-events=%u; ACK != motion-complete; desktop human-command consent only, no automatic clear/retry\n",
      link.running,link.ready,fresh(now_ms()),link.armed,link.fault,unsigned(link.config),link.impl,link.error,link.pid,
      link.tx,link.rx,link.bad,link.done_events);
    std::printf("mcu-v221: upstream=efdb437 heartbeat=250ms; impl-expected=3f (older partial firmware allowed by capability); blind=%u\n"
      "tof: age-ms=%llu mask=%02x distances-mm=%d,%d,%d,%d (-1 invalid); edge=%u obstacle=%u; event-positions=%lld,%lld (uncorrelated broadcasts)\n",
      unsigned((link.flags>>1)&1),(unsigned long long)(link.tof_ms?now_ms()-link.tof_ms:UINT64_MAX),link.tof_mask,
      link.distances[0],link.distances[1],link.distances[2],link.distances[3],link.edge_events,link.obstacle_events,
      (long long)link.positions[0],(long long)link.positions[1]);
    std::printf("mcu-desktop-v225: session=%u inhibited=%u; v227 UI/voice/tool steps x20, input=1..512, max=10240 pulses, default=512; move-raw is diagnostic pulses\n",link.desktop,link.desktop_inhibit);
    pthread_mutex_unlock(&lock);return 0;
  } else if((argc==1||(argc==2&&!std::strcmp(argv[1],"session")))&&!std::strcmp(argv[0],"arm")) {
    pthread_mutex_lock(&lock);
    rc=link.ready&&fresh(now_ms())&&!link.fault?0:-EACCES;
    if(!rc){link.armed=true;link.desktop_inhibit=false;link.arm_until=argc==2?UINT64_MAX:now_ms()+300000;}
    pthread_mutex_unlock(&lock);
    if(!rc)std::puts(argc==2?"MCU explicitly armed for this connection session; fault/heartbeat loss/STOP/reboot disarm. Keep tracks clear; no edge protection assumed.":"MCU motion armed for 5 minutes. Keep tracks clear; no edge protection is assumed.");
  } else if(argc==2&&!std::strcmp(argv[0],"desktop")&&(!std::strcmp(argv[1],"on")||!std::strcmp(argv[1],"off"))) {
    mcu_desktop_session(!std::strcmp(argv[1],"on"));
  } else if(argc==1&&(!std::strcmp(argv[0],"disarm")||!std::strcmp(argv[0],"disconnect"))) {
    pthread_mutex_lock(&lock);link.armed=false;link.urgent=true;
    link.desktop=false;
    if(!std::strcmp(argv[0],"disconnect"))link.quit=true;
    pthread_mutex_unlock(&lock);
    std::puts("Motion disarmed; best-effort STOP requested, ACK not implied.");
  } else if(argc==1&&!std::strcmp(argv[0],"ping"))rc=mcu_request({6,{}},text);
  else if(argc==1&&!std::strcmp(argv[0],"stop"))rc=mcu_request({12,{}},text);
  else if(argc==1&&(!std::strcmp(argv[0],"config-get")||!std::strcmp(argv[0],"stepper-stat")||!std::strcmp(argv[0],"link-stat")))
    rc=mcu_request({uint8_t(!std::strcmp(argv[0],"config-get")?8:!std::strcmp(argv[0],"stepper-stat")?9:10),{}},text);
  else if(argc==3&&!std::strcmp(argv[0],"config-set")&&!std::strcmp(argv[2],"confirm")) {
    char *end=nullptr;errno=0;unsigned long value=std::strtoul(argv[1],&end,16);
    if(errno||!argv[1][0]||!end||*end||value>0x7f)rc=-EINVAL;
    else {pthread_mutex_lock(&lock);link.armed=false;link.urgent=true;pthread_mutex_unlock(&lock);
      rc=mcu_request({7,{uint8_t(value),0,0,0}},text);}
  } else if(argc==2&&!std::strcmp(argv[0],"tof-poll")) {
    int ms=0;rc=number(argv[1],10,255,ms)?mcu_request({4,{uint8_t(ms)}},text):-EINVAL;
  } else if(argc==2&&!std::strcmp(argv[0],"tts")) {
    std::vector<uint8_t> frame;
    rc=std::strlen(argv[1])<=190&&tts_text_frame(argv[1],4,frame)&&frame.size()<=200?
       mcu_request({3,frame},text):-EINVAL;
  }
  else if((argc==1||argc==2)&&!std::strcmp(argv[0],"clear-fault")) {
    if(argc==2&&std::strcmp(argv[1],"blind-ack"))rc=-EINVAL;
    else {pthread_mutex_lock(&lock);link.armed=false;pthread_mutex_unlock(&lock);
      rc=mcu_request({5,argc==2?std::vector<uint8_t>{0,0x5a}:std::vector<uint8_t>{0}},text);}
    if(!rc)std::puts("Fault clear ACK received. Recheck ping before manual arm; this does not enable sensors.");
  } else if(argc==4&&(!std::strcmp(argv[0],"move")||!std::strcmp(argv[0],"move-raw"))) {
    int direction=-1,speed=0,steps=0;const char *names[]={"forward","backward","left","right"};
    for(int i=0;i<4;i++)if(!std::strcmp(argv[1],names[i]))direction=i;
    bool raw=!std::strcmp(argv[0],"move-raw");unsigned scale=raw?1:mcu_step_scale;
    if(direction<0||!number(argv[2],0,3,speed)||!number(argv[3],1,raw?10240:int(mcu_step_max),steps)||!mcu_move(direction,speed,unsigned(steps)*scale,c))rc=-EINVAL;
    else {std::printf("mcu-step-v227: input=%d scale=%u pulses=%u; not centimeters\n",steps,scale,unsigned(steps)*scale);rc=mcu_request(c,text);}
  } else if((argc==2||argc==3)&&!std::strcmp(argv[0],"servo")) {
    int angle=0,ms=500;
    if(!number(argv[1],-45,45,angle)||(argc==3&&!number(argv[2],100,2000,ms))||!mcu_servo(angle,ms,c))rc=-EINVAL;
    else rc=mcu_request(c,text);
  } else {
    std::puts("aipet mcu start|status|ping|arm|disarm|disconnect|stop\n"
              "aipet mcu desktop on|off (run enables this session; no fault bypass)\n"
              "aipet mcu config-get|stepper-stat|link-stat\n"
              "aipet mcu config-set 00..7f confirm (FLASH write; disarms; console only)\n"
              "aipet mcu tof-poll 10..255 | tts TEXT (optional MCU TW-TTS, not I2S)\n"
              "aipet mcu clear-fault [blind-ack] (console ONLY; no config writes)\n"
              "aipet mcu move forward|backward|left|right 0..3 1..512 (x20 pulses)\n"
              "aipet mcu move-raw forward|backward|left|right 0..3 1..10240 (unscaled pulses)\n"
              "aipet mcu servo -45..45 [100..2000 ms]");return 1;
  }
  if(!text.empty())std::puts(text.c_str());
  std::printf("mcu: result=%d\n",rc);return rc?1:0;
}
bool mcu_route(const char *text,bool inspect,std::string &reply,int *status)
{
  if(status)*status=0;
  McuCommand c;int intent=mcu_intent(text?text:"",c);
  if(!intent)return false;
  if(intent<0){if(status)*status=-EINVAL;reply="动作不明确，未发送。请一次只说前进、后退、左转、右转、停止或向左看。";return true;}
  if(inspect){reply="路由检查：UART4 下位机控制（不发送）";return true;}
  int rc=mcu_desktop_request(c,reply);if(status)*status=rc;return true;
}
int mcu_desktop_request(const McuCommand &c,std::string &reply) {
  if(c.op==1||c.op==2) {
    pthread_mutex_lock(&lock);
    if(link.desktop&&!link.desktop_inhibit&&link.ready&&fresh(now_ms())&&!link.fault) {
      link.armed=true;link.arm_until=UINT64_MAX;
    }
    pthread_mutex_unlock(&lock);
    if(c.op==1)std::printf("mcu-route-v225: pulses=%u speed=%u (v227 UI/voice/tool x20; ACK is not completion)\n",unsigned(c.payload[2])|(unsigned(c.payload[3])<<8),c.payload[1]);
  }
  return mcu_request(c,reply);
}
}
