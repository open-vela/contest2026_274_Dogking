/* Host ABI seam; actual SDK header is checked by the ARM64/full build. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef char *(*tool_provider_fn)(void);
typedef int (*tool_executor_fn)(const char *,const char *,char *,size_t);
void tool_registry_register_provider(const char *,tool_provider_fn,tool_executor_fn);
void tool_registry_invalidate(void);
#ifdef __cplusplus
}
#endif
