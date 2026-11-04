/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_VOICE_LOAD_LOG_HXX
#define AIPET_VOICE_LOAD_LOG_HXX
#include <cstdio>
#include <ctime>
namespace aipet {
inline long long voice_load_now() {
  timespec t{};
  if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
  return static_cast<long long>(t.tv_sec)*1000000+t.tv_nsec/1000;
}
/* Loading only, never the capture FIFO/hot inference loop. Flush each marker
 * so even a blocking file read or ORT session creation leaves a visible BEGIN.
 * Elapsed times include I/O/CPU/validation; they are NOT storage benchmarks. */
class VoiceLoadLog {
  const char *stage_,*path_;
  long long start_=voice_load_now();
public:
  VoiceLoadLog(const char *stage,const char *path="-"):stage_(stage),path_(path) {
    std::printf("voice-load-v215: BEGIN stage=%s path=%s mono-us=%lld\n",stage_,path_,start_);
    std::fflush(stdout);
  }
  void progress(unsigned long long bytes,unsigned long long total) const {
    std::printf("voice-load-v215: PROGRESS stage=%s bytes=%llu/%llu elapsed-ms=%lld\n",
      stage_,bytes,total,(voice_load_now()-start_)/1000);
    std::fflush(stdout);
  }
  int finish(int rc) const {
    std::printf("voice-load-v215: END stage=%s status=%d elapsed-ms=%lld\n",
      stage_,rc,(voice_load_now()-start_)/1000);
    std::fflush(stdout);return rc;
  }
};
}
#endif
