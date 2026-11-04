/* SPDX-License-Identifier: Apache-2.0 */
#include "voice_phrase_pipe.hxx"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>
#include <utility>
using namespace aipet;
static std::atomic<unsigned> calls{0};
static std::vector<std::string> spoken;
extern "C" void __lsan_disable();
extern "C" void __lsan_enable();
static int speak(void *, const char *s, VoiceLocalCancel cancel, void *cookie) {
  int rc = cancel(cookie);
  if (rc)
    return rc;
  spoken.emplace_back(s);
  ++calls;
  usleep(1000);
  return 0;
}
int main() {
  VoicePhrasePipe *s = nullptr;
  VoicePhraseMetrics m{};
  assert(!voice_phrase_open(speak, nullptr, &s));
  std::string first = "我记住了你的名字。";
  assert(!voice_phrase_accept(s, first.data(), first.size()));
  for (unsigned i = 0; i < 100 && !calls; i++)
    usleep(1000);
  assert(calls ==
         1); // Worker began before producer has submitted/finished the rest.
  std::string rest = "\n你喜欢童话故事。 ";
  assert(!voice_phrase_accept(s, rest.data(), rest.size()));
  assert(!voice_phrase_finish(s, 0, &m));
  assert(m.queued == 2 && m.spoken == 2 && spoken.size() == 2);
  assert(spoken[0] == first && spoken[1] == "你喜欢童话故事。");
  voice_phrase_close(s);
  spoken.clear();calls=0;
  assert(!voice_phrase_open(speak,nullptr,&s));
  std::string greeting="你好，";
  assert(!voice_phrase_accept(s,greeting.data(),greeting.size()));
  usleep(10000);
  assert(calls==0); // Do not make a two-syllable restart/gap after greeting.
  std::string tail="这是剩下的完整内容，不会被提前结束。";
  assert(!voice_phrase_accept(s,tail.data(),tail.size()));
  assert(!voice_phrase_finish(s,0,&m));
  std::string joined;for(const auto &piece:spoken)joined+=piece;
  assert(joined==greeting+tail);voice_phrase_close(s);
  spoken.clear();calls=0;joined.clear();
  assert(!voice_phrase_open(speak,nullptr,&s));
  std::string mixed="你好！我是MiniCPM系列模型，由团队开发。🦊之后继续完整回答你的问题。";
  assert(!voice_phrase_accept(s,mixed.data(),mixed.size()));
  assert(!voice_phrase_finish(s,0,&m));
  for(const auto &piece:spoken)joined+=piece;
  bool whole_word=false;for(const auto &piece:spoken)if(piece.find("MiniCPM")!=std::string::npos)whole_word=true;
  assert(whole_word); // no arbitrary split inside an English name
  assert(joined==mixed && m.spoken==m.queued && m.spoken>=3);voice_phrase_close(s);
  for (auto bad : {"<|im_start|>", "{\"tool\":1}", "[开心]", "\xed\xa0\x80",
                   "\xf0\x80\x80\x80"}) {
    assert(!voice_phrase_open(speak, nullptr, &s));
    unsigned before = calls;
    assert(voice_phrase_accept(s, bad, std::char_traits<char>::length(bad)) <
           0);
    assert(voice_phrase_finish(s, 0, &m) < 0 && calls == before);
    voice_phrase_close(s);
  }
  assert(!voice_phrase_open(
      [](void *, const char *, VoiceLocalCancel, void *) { return -EIO; },
      nullptr, &s));
  assert(!voice_phrase_accept(s, first.data(), first.size()));
  assert(voice_phrase_finish(s, 0, &m) == -EIO);
  voice_phrase_close(s);
  puts("PASS: synthesis overlaps generation, UTF8/role/tool rejection, "
       "whitespace, bounded phrases and error propagation (host mocks)");
  VoiceLocalCancel retained_cancel=nullptr;void *retained_cookie=nullptr;
  __lsan_disable();
  // Test uses an explicit caller cookie with scope until finish; the retained
  // I2S cancellation cookie is the heap phrase state, never this pair.
  std::pair<VoiceLocalCancel*,void**> destination{&retained_cancel,&retained_cookie};
  assert(!voice_phrase_open(
      [](void *p,const char *,VoiceLocalCancel cancel,void *cookie) {
        auto *pair=static_cast<std::pair<VoiceLocalCancel*,void**> *>(p);
        *pair->first=cancel;*pair->second=cookie;return -EOWNERDEAD;
      },&destination,&s));
  assert(!voice_phrase_accept(s,first.data(),first.size()));
  assert(voice_phrase_finish(s,0,&m)==-EOWNERDEAD);
  voice_phrase_close(s); // Must NOT free the downstream cancellation cookie.
  __lsan_enable();
  assert(retained_cancel && retained_cookie && retained_cancel(retained_cookie)==-EOWNERDEAD);
  puts("PASS: downstream retained-worker cancellation chain survives close (intentional reboot-lifetime state)");
}
