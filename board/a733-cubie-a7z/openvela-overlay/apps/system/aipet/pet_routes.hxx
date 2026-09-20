/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "pet_core.hxx"
namespace aipet {
/* Future local module supplies a process-lifetime, thread-safe callback.
 * Not registered by default; no model loading in the online-only build. */
using LocalRouteBackend = int (*)(const char *text, char *reply, std::size_t cap);
void set_local_route_backend(LocalRouteBackend backend);
Rules default_routes();
/* Ordered Linux rules schema; bounded, no credentials or Python imports. */
bool load_routes(const char *path, Rules &out);
std::string expand_clock_template(const std::string &text);
int routed_ask(const char *text, bool inspect, bool force_cloud);
int routed_local_ask(const char *text);
int uart_tts_control(const char *command, const char *text = nullptr);
}
