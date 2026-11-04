/* SPDX-License-Identifier: Apache-2.0 */
#include "uart_tts.hxx"
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <chrono>

namespace aipet
{
bool tts_text_frame(const std::string &text, std::uint8_t encoding,
                    std::vector<std::uint8_t> &frame)
{
  frame.clear();
  if (text.empty() || text.size() > 4094 || text.find('\0') != std::string::npos ||
      (encoding != 0x00 && encoding != 0x04))
    return false;
  const auto length = text.size() + 2;
  frame = {0xfd, static_cast<std::uint8_t>(length >> 8),
           static_cast<std::uint8_t>(length), 1, encoding};
  frame.insert(frame.end(), text.begin(), text.end());
  return true;
}

bool tts_parameter_frame(char parameter, int level,
                         std::vector<std::uint8_t> &frame)
{
  frame.clear();
  if (parameter != 'v' && parameter != 's' && parameter != 't') return false;
  if (level < 0) level = 0;
  if (level > 9) level = 9;
  frame = {0xfd, 0, 6, 1, 1, '[', static_cast<std::uint8_t>(parameter),
           static_cast<std::uint8_t>('0' + level), ']'};
  return true;
}

bool UartTts::write_frame(int fd, const std::vector<std::uint8_t> &frame)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  std::size_t sent = 0;
  while (sent < frame.size())
    {
      if (std::chrono::steady_clock::now() >= deadline)
        { errno = ETIMEDOUT; return false; }
      const auto written = write(fd, frame.data() + sent, frame.size() - sent);
      if (written < 0)
        { if (errno == EINTR || errno == EAGAIN) continue; return false; }
      if (!written) { errno = EIO; return false; }
      sent += static_cast<std::size_t>(written);
    }
  ++status_.frames;
  return true;
}

bool UartTts::speak_utf8(const std::string &text, int volume, int speed, int tone)
{
  status_.stage = "validate";
  std::vector<std::uint8_t> speech;
  if (!tts_text_frame(text, 0x04, speech) ||
      device_.compare(0, 5, "/dev/") != 0 ||
      device_ == "/dev/console" || device_ == "/dev/ttyS0" ||
      device_.find("..") != std::string::npos)
    { ++status_.failures; status_.last_errno = EINVAL; return false; }
  status_.stage = "open";
  const int fd = open(device_.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) { ++status_.failures; status_.last_errno = errno; return false; }
  status_.stage = "termios";
  termios saved{}, configured{};
  bool have_saved = tcgetattr(fd, &saved) == 0;
  bool ok = have_saved;
  if (ok)
    {
      configured = saved;
      configured.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
      configured.c_cflag |= CS8 | CLOCAL | CREAD;
      configured.c_oflag = 0;
      ok = cfsetospeed(&configured, B9600) == 0 &&
           cfsetispeed(&configured, B9600) == 0 &&
           tcsetattr(fd, TCSANOW, &configured) == 0;
    }
  if (ok && !status_.initialized)
    {
      status_.stage = "parameters";
      const char names[] = {'v', 's', 't'};
      const int levels[] = {volume, speed, tone};
      for (unsigned i = 0; i < 3 && ok; ++i)
        {
          std::vector<std::uint8_t> frame;
          ok = tts_parameter_frame(names[i], levels[i], frame) && write_frame(fd, frame);
          if (ok) usleep(i == 2 ? 300000 : 100000);
        }
      status_.initialized = ok;
    }
  if (ok)
    {
      status_.stage = "text";
      ok = write_frame(fd, speech);
    }
  const int failure = ok ? 0 : errno;
  /* Restoring immediately could change baud while data is still queued.
   * Keep the explicitly dedicated TTS port configured; do not touch console. */
  close(fd);
  status_.last_errno = failure;
  if (!ok) { ++status_.failures; status_.initialized = false; }
  else status_.stage = "complete";
  return ok;
}
}
