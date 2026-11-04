/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Production: no background debug output. Development: compile with
 * -DAIPET_DEBUG=1. Never log prompts, credentials, audio or raw replies. */
#ifndef AIPET_DEBUG
#define AIPET_DEBUG 0
#endif
#if AIPET_DEBUG
#include <cstdio>
#endif
namespace aipet
{
inline void trace_phase(const char *phase, int status)
{
#if AIPET_DEBUG
  std::fprintf(stderr, "[aipet] phase=%s status=%d\n", phase, status);
#else
  (void)phase;
  (void)status;
#endif
}
}
