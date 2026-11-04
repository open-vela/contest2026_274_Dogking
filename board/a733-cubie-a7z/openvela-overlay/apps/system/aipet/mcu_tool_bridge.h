/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Keep the official C-only agent_compat inline definitions out of C++ TUs. */
void aipet_mcu_register_provider(char *(*tools)(void),
  int (*execute)(const char *,const char *,char *,size_t));
#ifdef __cplusplus
}
#endif
