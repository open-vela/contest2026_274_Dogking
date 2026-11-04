/* SPDX-License-Identifier: Apache-2.0 */
#include "mcu_tool_bridge.h"
#include "tools/tool_registry.h"
void aipet_mcu_register_provider(char *(*tools)(void),
  int (*execute)(const char *,const char *,char *,size_t))
{
  tool_registry_register_provider("stm32-uart4",tools,execute);
  tool_registry_invalidate();
}
