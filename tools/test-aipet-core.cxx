/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_core.hxx"
#include <cassert>
#include <iostream>
#include <string>

class Mock : public aipet::Ports
{
public:
  int network = 0, local = 0, cloud = 0, shown = 0, idle = 0;
  bool connected = true, cloud_ok = true, local_ok = true;
  aipet::Reply received;
  bool online() override { network++; return connected; }
  bool local_reply(const std::string &, std::string &r) override
    { local++; r = "本地回复[ACTION:servo.nod()][开心]"; return local_ok; }
  bool cloud_reply(const std::string &, std::string &r) override
    { cloud++; r = "云端回复[思考]"; return cloud_ok; }
  std::size_t choose(std::size_t) override { return 0; }
  std::string expand_template(const std::string &r) override { return r; }
  bool present(const aipet::Reply &r) override { shown++; received = r; return true; }
  void return_idle() override { idle++; }
};

static std::string hex(const std::string &s)
{
  std::string r;
  const char *digits = "0123456789abcdef";
  for (unsigned char c : s) { r += digits[c >> 4]; r += digits[c & 15]; }
  return r;
}
static std::string unhex(const std::string &s)
{
  std::string r;
  for (std::size_t i = 0; i + 1 < s.size(); i += 2)
    r += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
  return r;
}
int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--parse-lines")
    {
      std::string line;
      while (std::getline(std::cin, line))
        {
          aipet::Reply reply;
          if (!aipet::parse_reply(unhex(line), reply)) { std::cout << "REJECT\n"; continue; }
          std::cout << hex(reply.text) << '\t' << hex(reply.emotion);
          for (const auto &action : reply.actions) std::cout << '\t' << hex(action);
          std::cout << '\n';
        }
      return 0;
    }
  aipet::Reply parsed;
  const char *extra[] = {"疑惑", "困惑", "好奇", "兴奋", "得意", "害羞", "害怕", "期待", "无语", "鄙视", "委屈", "调皮", "赞"};
  for (const char *emotion : extra)
    {
      assert(aipet::parse_reply(std::string("你好[") + emotion + "]", parsed));
      assert(parsed.text == "你好" && parsed.emotion == emotion);
    }
  const std::string streamed = "好的！[ACTION:servo.nod()][期待]下一句。";
  for (std::size_t split = 0; split <= streamed.size(); ++split)
    {
      aipet::SentenceStream stream;
      std::vector<aipet::Reply> ready;
      assert(stream.feed(streamed.substr(0, split), ready));
      assert(stream.feed(streamed.substr(split), ready));
      assert(stream.finish(ready));
      assert(ready.size() == 2 && ready[0].text == "好的！");
      assert(ready[0].emotion == "期待" && ready[0].actions.size() == 1);
      assert(ready[1].text == "下一句。");
    }
  {
    aipet::SentenceStream stream;
    std::vector<aipet::Reply> ready;
    assert(stream.feed("好的！[ACTION:servo.", ready) && ready.empty());
    stream.cancel();
    assert(!stream.finish(ready) && ready.empty());
  }
  assert(aipet::parse_reply("好的！[ACTION:motor.forward(2)][开心]", parsed));
  assert(parsed.text == "好的！" && parsed.emotion == "开心" && parsed.actions.size() == 1);
  assert(aipet::parse_reply("[赞]a[开心][ACTION:unknown.call(x)]b", parsed));
  assert(parsed.text == "ab" && parsed.emotion == "赞" && parsed.actions.empty());
  assert(aipet::parse_reply("[ACTION:Servo.nod()][ACTION:servo.nod(]", parsed));
  assert(parsed.text == "[ACTION:Servo.nod()][ACTION:servo.nod(]" && parsed.actions.empty());
  assert(aipet::parse_reply("a[ACTION:servo.nod(x\ny)]b", parsed));
  assert(parsed.text == "ab" && parsed.actions.size() == 1);
  assert(!aipet::parse_reply(std::string(4097, 'x'), parsed) && parsed.actions.empty());
  assert(!aipet::parse_reply(std::string("\xe4\xbd", 2), parsed));
  std::string many;
  for (int i = 0; i < 33; i++) many += "[ACTION:servo.nod()]";
  assert(!aipet::parse_reply(many, parsed) && parsed.actions.empty());
  aipet::Rules rules;
  rules.direct.push_back({{"hello", "你好"}, {"嗨！[开心]"}});
  rules.local.push_back({{"笑话"}, {}});
  Mock ports;
  aipet::Conversation conversation(rules, ports);
  assert(conversation.run("HELLO 笑话").route == aipet::Route::direct);
  assert(ports.network == 0 && ports.local == 0 && ports.cloud == 0);
  assert(conversation.run("笑话").route == aipet::Route::cloud && ports.network == 1);
  assert(ports.received.text == "云端回复");
  assert(conversation.run("写诗").route == aipet::Route::cloud && ports.cloud == 2);
  ports.cloud_ok = false;
  auto fallback = conversation.run("写诗");
  assert(fallback.ok && fallback.cloud_fallback && fallback.route == aipet::Route::local);
  ports.connected = false;
  auto offline = conversation.run("写诗");
  assert(offline.ok && !offline.cloud_fallback && offline.route == aipet::Route::local);
  ports.local_ok = false;
  int shown = ports.shown;
  assert(!conversation.run("写诗").ok && ports.shown == shown);
  assert(conversation.state() == aipet::State::idle && conversation.completed() == 5);
  assert(conversation.run(" \t ").ok); /* Original router's empty input reply. */
  assert(ports.received.text == "嗯？我没听清，再说一次吧~");
  std::cout << "aipet parser/routing/fallback/state tests passed\n";
}
