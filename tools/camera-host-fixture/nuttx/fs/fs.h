/* SPDX-License-Identifier: Apache-2.0 */
/* HOST TEST STUB ONLY; actual SDK file_operations checked by ARM compile. */
#include <sys/types.h>
struct file {void *f_priv; off_t f_pos;};
struct file_operations
{
  int (*open)(struct file *);
  int (*close)(struct file *);
  ssize_t (*read)(struct file *, char *, size_t);
};
int register_driver(const char *, const struct file_operations *, mode_t, void *);
