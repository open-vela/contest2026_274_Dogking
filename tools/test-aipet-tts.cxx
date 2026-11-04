/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/uart_tts.hxx"
#include <cassert>
#include <iostream>
int main()
{
  std::vector<std::uint8_t> frame;
  assert(aipet::tts_text_frame("Hello", 0x04, frame));
  assert((frame == std::vector<std::uint8_t>{0xfd,0,7,1,4,'H','e','l','l','o'}));
  assert(aipet::tts_text_frame(std::string("\xc4\xe3\xba\xc3",4), 0x00, frame));
  assert(frame.size() == 9 && frame[2] == 6);
  assert(aipet::tts_text_frame("你好", 0x04, frame));
  assert(!aipet::tts_text_frame("Hello", 0x03, frame));
  assert(!aipet::tts_text_frame(std::string(4095,'a'), 0x04, frame));
  assert(aipet::tts_text_frame(std::string(4094,'a'), 0x04, frame));
  assert(frame[1] == 16 && frame[2] == 0);
  assert(aipet::tts_parameter_frame('v',99,frame));
  assert(frame[7] == '9' && frame.size() == 9);
  assert(!aipet::tts_parameter_frame('x',5,frame));
  aipet::UartTts port("/dev/console");
  assert(!port.speak_utf8("Hello",5,5,5));
  assert(port.status().failures == 1);
  std::cout << "TW-TTS framing and safety tests passed\n";
}
