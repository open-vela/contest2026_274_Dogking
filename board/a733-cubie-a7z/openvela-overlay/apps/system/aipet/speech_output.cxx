/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_output.hxx"
#include <cerrno>
#include <new>

namespace aipet
{
static bool valid_text(const std::string &text)
{
  return !text.empty() && text.size() <= 4096 &&
         text.find('\0') == std::string::npos;
}

int UartSpeechOutput::speak(const std::string &utf8)
{
  if (!valid_text(utf8)) return -EINVAL;
  if (port_.speak_utf8(utf8, 5, 5, 5)) return 0;
  const int error = port_.status().last_errno;
  return -(error ? error : EIO);
}

int UartSpeechOutput::stop() { return -ENOSYS; }

int PcmSpeechOutput::speak(const std::string &text)
{
  if (!valid_text(text)) return -EINVAL;
  if (!ops_.synthesize || !ops_.play) return -ENODEV;
  /* Reserved bounded whole-utterance path. Long or streaming audio needs a
   * later chunked adapter, not silent truncation to this buffer. */
  constexpr std::size_t capacity = 320000; /* 10 s at 16kHz/s16/mono */
  unsigned char *pcm = new (std::nothrow) unsigned char[capacity];
  if (!pcm) return -ENOMEM;
  std::size_t length = 0;
  int result = ops_.synthesize(ops_.context, text.c_str(), pcm, capacity,
                               &length);
  if (result == 0 && (!length || length > capacity || (length & 1)))
    result = -EMSGSIZE;
  if (result == 0)
    result = ops_.play(ops_.context, pcm, length, 16000, 1, 16);
  delete[] pcm;
  return result;
}

int PcmSpeechOutput::stop()
{
  return ops_.stop ? ops_.stop(ops_.context) : -ENODEV;
}
}
