/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic HOST scheduler only. */
#include <stdint.h>
typedef unsigned sem_t;
int nxsem_init(sem_t *,int,unsigned);
int nxsem_reset(sem_t *,int16_t);
int nxsem_post(sem_t *);
int nxsem_tickwait_uninterruptible(sem_t *,unsigned);
