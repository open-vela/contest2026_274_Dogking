/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pet_core.hxx"
#include "speech_output.hxx"
#include <atomic>
#include <cerrno>
#include <memory>
#include <new>
#include <pthread.h>
#include <unistd.h>
namespace aipet {
/* Two owned PCM slots. Only the caller plays; one joined worker synthesizes
 * the next COMPLETE clause. No action/text retry, no partial audio replay. */
inline int cloud_clause_pipeline(const std::vector<Reply> &clauses,PcmSpeechOps ops,
                                int (*cancel)(void *),void *cookie,
                                std::atomic<bool> &stop,unsigned &done) {
  constexpr std::size_t cap=320000;done=0;
  if(clauses.empty())return 0;
  struct Job {
    PcmSpeechOps ops;const char *text;unsigned char *pcm;std::size_t bytes=0;
    std::atomic<bool> finished{false};int status=-EAGAIN;
  };
  std::unique_ptr<unsigned char[]> a(new(std::nothrow) unsigned char[cap]);
  std::unique_ptr<unsigned char[]> b(clauses.size()>1?new(std::nothrow) unsigned char[cap]:nullptr);
  if(!a||(clauses.size()>1&&!b))return -ENOMEM;
  auto check=[&](){int rc=cancel?cancel(cookie):0;if(rc)stop=true;return rc;};
  auto valid=[](std::size_t n){return n&&n<=cap&&!(n&1);};
  std::size_t bytes=0;int rc=check();
  if(!rc)rc=ops.synthesize(ops.context,clauses[0].text.c_str(),a.get(),cap,&bytes);
  if(!rc&&!valid(bytes))rc=-EMSGSIZE;
  for(std::size_t i=0;!rc&&i<clauses.size();i++) {
    if((rc=check()))break;
    Job next{ops,i+1<clauses.size()?clauses[i+1].text.c_str():nullptr,b.get()};
    pthread_t task{};bool running=false;
    if(next.text) {
      pthread_attr_t attr;int e=pthread_attr_init(&attr);
      if(!e) {
        e=pthread_attr_setstacksize(&attr,65536);
        if(!e)e=pthread_create(&task,&attr,[](void *p)->void * {
          auto &j=*static_cast<Job *>(p);
          j.status=j.ops.synthesize(j.ops.context,j.text,j.pcm,cap,&j.bytes);
          j.finished.store(true,std::memory_order_release);return nullptr;
        },&next);
        pthread_attr_destroy(&attr);
      }
      // Resource failure falls back to serial, not a dropped tail.
      running=!e;
    }
    rc=ops.play(ops.context,a.get(),bytes,16000,1,16);
    if(!rc)++done;else stop=true;
    if(running) {
      while(!next.finished.load(std::memory_order_acquire)) {
        int cancelled=check();if(!rc&&cancelled)rc=cancelled;
        usleep(20000);
      }
      int joined=pthread_join(task,nullptr);
      if(joined) { // Never return while a worker might borrow stack/buffers.
        for(;;)usleep(100000);
      }
      if(!rc)rc=next.status;
      if(!rc&&!valid(next.bytes))rc=-EMSGSIZE;
      bytes=next.bytes;a.swap(b);
    } else if(!rc&&next.text) {
      rc=check();if(!rc)rc=ops.synthesize(ops.context,next.text,a.get(),cap,&bytes);
      if(!rc&&!valid(bytes))rc=-EMSGSIZE;
    }
  }
  return rc;
}
}
