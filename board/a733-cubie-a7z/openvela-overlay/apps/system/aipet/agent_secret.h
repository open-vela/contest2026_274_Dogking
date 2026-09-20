/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int aipet_secret_seal(const char *plain, char *out, size_t cap);
int aipet_secret_open(const char *sealed, char *out, size_t cap);
void aipet_wipe(void *p, size_t n);
int aipet_setup(void);
#ifdef __cplusplus
}
#endif
