/* SPDX-License-Identifier: Apache-2.0 */
typedef int mutex_t;
#define NXMUTEX_INITIALIZER 0
int nxmutex_trylock(mutex_t *);
int nxmutex_lock(mutex_t *);
int nxmutex_unlock(mutex_t *);
