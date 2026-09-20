/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "uart_tts.hxx"
#include <cstddef>
#include <cstdint>

namespace aipet
{
/* Business-layer text output. UART is a direct-speaking device, whereas the
 * future official voice_tts backend produces PCM for Media playback. */
class SpeechOutput
{
public:
  virtual ~SpeechOutput() = default;
  virtual int speak(const std::string &utf8) = 0;
  virtual int stop() = 0;
};

class UartSpeechOutput : public SpeechOutput
{
public:
  explicit UartSpeechOutput(UartTts &port) : port_(port) {}
  int speak(const std::string &utf8) override;
  int stop() override; /* Protocol stop command is not yet verified. */
private:
  UartTts &port_;
};

/* Reserved adapter contract, not an enabled I2S driver. Synthesis corresponds
 * to voice_tts_speak (s16le/16kHz/mono); playback/stop belong to official Media
 * or audio_playback. Callback contexts remain caller-owned. */
struct PcmSpeechOps
{
  void *context = nullptr;
  int (*synthesize)(void *, const char *, unsigned char *, std::size_t,
                    std::size_t *) = nullptr;
  int (*play)(void *, const unsigned char *, std::size_t,
              unsigned rate, unsigned channels, unsigned bits) = nullptr;
  int (*stop)(void *) = nullptr;
};

class PcmSpeechOutput : public SpeechOutput
{
public:
  explicit PcmSpeechOutput(PcmSpeechOps ops = {}) : ops_(ops) {}
  int speak(const std::string &utf8) override;
  int stop() override;
private:
  PcmSpeechOps ops_;
};
}
