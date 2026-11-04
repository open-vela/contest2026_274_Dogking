/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cerrno>
#include <cstdio>
#if defined(__NuttX__) || defined(AIPET_TEST_COMPUTE)
#include <sched.h>
#include <ctime>
#include <unistd.h>
#include <system/a733_cpu_qualify.h>
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#endif
namespace aipet {
/* One scoped calling thread: ASR/staging CPU6, CPU acoustic CPU7.
 * Successful setaffinity alone is not migration proof. No global DVFS,
 * scheduler change, persistent ORT worker or cross-task-group fd. */
class VoiceCompute {
public:
  explicit VoiceCompute(unsigned cpu=7,unsigned part=0xd0b):target(cpu),expected_part(part) {}
  int begin() {
#if defined(__NuttX__) || defined(AIPET_TEST_COMPUTE)
#ifdef __NuttX__
    if(CONFIG_SMP_NCPUS!=8)return -ENOTSUP;
#endif
    if(active)return -EBUSY;
    if(sched_getaffinity(0,sizeof(saved),&saved)<0)return -errno;
    const a733_cpu_ops ops{get,set,identity,timer,wait,stop};
    a733_cpu_proof p{};
    int r=a733_cpu_qualify(&ops,this,target,expected_part,&p);
    static unsigned reported=0;
    if(r || !(__atomic_fetch_or(&reported,1u<<target,__ATOMIC_RELAXED)&(1u<<target)))
      std::printf("voice-compute-v210: target=%u actual=%llu part=%03llx mask=%08lx confirmations=%u attempts=%u elapsed-us=%llu result=%d restore=%d\n",
        target,static_cast<unsigned long long>((p.mpidr>>8)&255),
        static_cast<unsigned long long>((p.midr>>4)&4095),
        static_cast<unsigned long>(p.readback),p.confirmations,p.attempts,
        static_cast<unsigned long long>(p.elapsed),r,p.restore_error);
    if(r)return r;
    active=true;
#endif
    return 0;
  }
  int finish(int rc) {
#if defined(__NuttX__) || defined(AIPET_TEST_COMPUTE)
    if(active) {
      active=false;int r=0;cpu_set_t back{};
      if(sched_setaffinity(0,sizeof(saved),&saved)<0)r=-errno;
      if(!r && sched_getaffinity(0,sizeof(back),&back)<0)r=-errno;
      if(!r && !CPU_EQUAL(&saved,&back))r=-EIO;
      /* Restoring a restricted old mask can also defer physical migration.
       * Default all-core masks already allow the current CPU: no wait needed. */
      for(unsigned n=0;!r;n++) {
        uint64_t midr=0,mpidr=0;r=identity(this,&midr,&mpidr);
        unsigned cpu=static_cast<unsigned>((mpidr>>8)&255);
        if(!r && cpu<8 && CPU_ISSET(cpu,&saved))break;
        if(r && r!=-EAGAIN)break;
        if(n==250){r=-ETIMEDOUT;break;}
        r=0;wait(this);
      }
      if(r) {std::printf("voice-compute-v210: restore=%d operation=%d\n",r,rc);return r;}
    }
#endif
    return rc;
  }
  ~VoiceCompute(){(void)finish(0);}
  VoiceCompute(const VoiceCompute &)=delete;
  VoiceCompute &operator=(const VoiceCompute &)=delete;
private:
  unsigned target;
  unsigned expected_part;
#if defined(__NuttX__) || defined(AIPET_TEST_COMPUTE)
  cpu_set_t saved{};bool active=false;
  static int get(void *,uint32_t *out) {
    cpu_set_t m{};if(sched_getaffinity(0,sizeof(m),&m)<0)return -errno;
    *out=0;for(unsigned i=0;i<8;i++)if(CPU_ISSET(i,&m))*out|=1u<<i;return 0;
  }
  static int set(void *,uint32_t bits) {
    cpu_set_t m{};CPU_ZERO(&m);
    for(unsigned i=0;i<8;i++)if(bits&(1u<<i))CPU_SET(i,&m);
    return sched_setaffinity(0,sizeof(m),&m)<0?-errno:0;
  }
  static int identity(void *,uint64_t *midr,uint64_t *mpidr) {
#ifdef AIPET_TEST_COMPUTE
    return aipet_compute_test_identity(midr,mpidr);
#elif defined(__aarch64__)
    uint64_t after;
    __asm__ volatile("mrs %0, mpidr_el1":"=r"(*mpidr)::"memory");
    __asm__ volatile("mrs %0, midr_el1":"=r"(*midr)::"memory");
    __asm__ volatile("mrs %0, mpidr_el1":"=r"(after)::"memory");
    return after==*mpidr?0:-EAGAIN;
#else
    (void)midr;(void)mpidr;return -ENOTSUP;
#endif
  }
  static uint64_t timer(void *) {
#ifdef AIPET_TEST_COMPUTE
    return aipet_compute_test_time();
#else
    timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
    return uint64_t(t.tv_sec)*1000000+t.tv_nsec/1000;
#endif
  }
  static void wait(void *) {
#ifdef AIPET_TEST_COMPUTE
    aipet_compute_test_wait();
#else
    usleep(1000);
#endif
  }
  static int stop(void *){return 0;}
#endif
};
}
