/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace aipet
{
struct Reply
{
  std::string text;
  std::string emotion = "普通";
  std::vector<std::string> actions;
};

/* Matches vision-4/src/action_parser.py followed by emotion_parser.py.
 * Complete replies only: never execute tags while generation is incomplete.
 * Malformed tags stay visible, as in the Linux implementation. */
bool parse_reply(const std::string &raw, Reply &out);

/* Single-owner incremental output. Tags following punctuation belong to that
 * sentence. No incomplete tag may reach an actuator. Cancel drops pending text.
 * A caller must stop feeding after any false return. */
class SentenceStream
{
public:
  bool feed(const std::string &chunk, std::vector<Reply> &ready);
  bool finish(std::vector<Reply> &ready);
  void cancel() { pending_.clear(); failed_ = true; }
private:
  bool drain(bool final, std::vector<Reply> &ready);
  std::string pending_;
  std::size_t bytes_ = 0;
  bool failed_ = false;
};

struct Rule
{
  std::vector<std::string> keywords;
  std::vector<std::string> replies;
};
struct Rules
{
  std::vector<Rule> direct;  /* JSON insertion order is significant. */
  std::vector<Rule> local;
};
enum class Route { direct, local, cloud };
enum class State { idle, routing, generating, presenting, error };

/* Adapters, not shell commands: local inference API, libcurl, Media API,
 * UIkit and actuator HAL belong here. No adapter is assumed available. */
class Ports
{
public:
  virtual ~Ports() = default;
  virtual bool online() = 0;
  virtual bool local_reply(const std::string &user, std::string &reply) = 0;
  virtual bool cloud_reply(const std::string &user, std::string &reply) = 0;
  virtual std::size_t choose(std::size_t count) = 0;
  virtual std::string expand_template(const std::string &reply) = 0;
  /* Must queue actions non-blockingly, set emotion, speak cleaned text.
   * Must validate numeric action arguments before any real actuator use. */
  virtual bool present(const Reply &reply) = 0;
  virtual void return_idle() = 0;
};

struct Turn
{
  bool ok = false;
  bool cloud_fallback = false;
  Route route = Route::direct;
  Reply reply;
};

class Conversation
{
public:
  Conversation(const Rules &rules, Ports &ports) : rules_(rules), ports_(ports) {}
  Turn run(const std::string &user);
  State state() const { return state_; }
  std::size_t completed() const { return completed_; }
private:
  Rules rules_;
  Ports &ports_;
  State state_ = State::idle;
  std::size_t completed_ = 0;
};
}
