/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aipet
{
/* TW-TTS protocol, identical to the validated Linux implementation.
 * Encoding 0x04 is the module's native UTF-8 selector. */
bool tts_text_frame(const std::string &text, std::uint8_t encoding,
                    std::vector<std::uint8_t> &frame);
bool tts_parameter_frame(char parameter, int level,
                         std::vector<std::uint8_t> &frame);

struct TtsStatus
{
  unsigned frames = 0;
  unsigned failures = 0;
  int last_errno = 0;
  bool initialized = false;
  const char *stage = "idle";
};

/* Caller owns one instance and serializes access. Device is explicitly
 * configured; never infer Linux ttyAS4 == openvela ttyS4 or use console. */
class UartTts
{
public:
  explicit UartTts(std::string device) : device_(std::move(device)) {}
  bool speak_utf8(const std::string &text, int volume, int speed, int tone);
  void reset() { status_.initialized = false; status_.stage = "idle"; }
  const TtsStatus &status() const { return status_; }
private:
  bool write_frame(int fd, const std::vector<std::uint8_t> &frame);
  std::string device_;
  TtsStatus status_;
};
}
