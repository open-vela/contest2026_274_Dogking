/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <memory>
#include <string>
namespace aipet_cloud {
/* One owner per TLS context. No concurrent SSL read/write threads. */
class WebSocket {
  struct State;std::unique_ptr<State> s;
public:
  WebSocket(int (*cancel)(void *)=nullptr,void *cookie=nullptr);~WebSocket();
  int open(const char *host,const std::string &signed_path,const char *port="443");
  int send(const std::string &json);
  int receive(std::string &json);
  bool readable();
  /* A fresh bounded service budget after connection/upload, not the leftover
   * TLS establishment budget. Progress never extends the hard cap. */
  void service_budget(unsigned total_ms=30000,unsigned idle_ms=8000);
  void close();
};
}
