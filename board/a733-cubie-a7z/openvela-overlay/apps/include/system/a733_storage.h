/* SPDX-License-Identifier: Apache-2.0 */
#ifndef A733_STORAGE_H
#define A733_STORAGE_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool a733_storage_ready(void);
void a733_storage_status(void);
int a733_storage_start(bool background);
int a733_storage_unmount(void);
#ifdef __cplusplus
}
#endif
#endif
