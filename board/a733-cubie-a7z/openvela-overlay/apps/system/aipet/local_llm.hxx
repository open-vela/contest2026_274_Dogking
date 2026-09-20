/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

namespace aipet {
void install_local_llm_backend();
int local_llm_control(const char *command, const char *value = nullptr);
int routed_local_ask(const char *text);
}
