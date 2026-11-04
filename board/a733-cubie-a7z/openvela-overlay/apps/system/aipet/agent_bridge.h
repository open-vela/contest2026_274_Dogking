#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Attach only after the official agent has initialized its bus and dispatcher.
 * This adapter never initializes/destroys or drains the shared bus itself. */
int aipet_agent_attach(void);
int aipet_agent_claim(void);
void aipet_agent_finish(int status);
void aipet_agent_status(int *phase, int *pid, int *error);
int aipet_startup(int background_boot);
void aipet_startup_status(void);
int aipet_agent_submit(const char *text, uint32_t timeout_ms);
/* Nonblocking: -EAGAIN while waiting; copies a complete reply on success.
 * Call from the pet owner, not the official outbound dispatcher. */
int aipet_agent_poll(char *reply, size_t capacity, uint64_t *elapsed_ms);
void aipet_agent_cancel(void);
void aipet_agent_detach(void);
#ifdef __cplusplus
}
#endif
