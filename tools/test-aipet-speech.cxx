/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/speech_output.hxx"
#include <cassert>
#include <cerrno>
#include <iostream>
struct Mock { unsigned calls = 0; std::size_t length = 4; };
static int synth(void *context, const char *, unsigned char *data,
                  std::size_t capacity, std::size_t *length)
{
  auto &mock = *static_cast<Mock *>(context);
  assert(capacity == 320000); data[0] = 1; *length = mock.length; return 0;
}
static int play(void *context, const unsigned char *data, std::size_t size,
                 unsigned rate, unsigned channels, unsigned bits)
{
  assert(data[0] == 1 && size == 4 && rate == 16000 && channels == 1 && bits == 16);
  ++static_cast<Mock *>(context)->calls; return 0;
}
static int stop(void *) { return 0; }
int main()
{
  aipet::PcmSpeechOutput disabled;
  assert(disabled.speak("你好") == -ENODEV && disabled.stop() == -ENODEV);
  Mock mock;
  aipet::PcmSpeechOutput pcm({&mock,synth,play,stop});
  assert(pcm.speak("你好") == 0 && mock.calls == 1);
  mock.length = 3; assert(pcm.speak("你好") == -EMSGSIZE && mock.calls == 1);
  mock.length = 320002; assert(pcm.speak("你好") == -EMSGSIZE && mock.calls == 1);
  assert(pcm.stop() == 0);
  aipet::UartTts uart("/dev/console");
  aipet::UartSpeechOutput speech(uart);
  assert(speech.speak("你好") == -EINVAL && speech.stop() == -ENOSYS);
  assert(speech.speak("") == -EINVAL);
  std::cout << "speech output contracts passed (mock PCM; I2S disabled)\n";
}
