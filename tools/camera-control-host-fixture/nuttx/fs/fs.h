/* SPDX-License-Identifier: Apache-2.0 */
/* HOST TEST ONLY. Real SDK fops separately checked by AArch64 compilation. */
#include <sys/types.h>
struct file {void *f_priv;off_t f_pos;int f_oflags;};
struct file_operations
{
  int (*open)(struct file *);
  int (*close)(struct file *);
  ssize_t (*read)(struct file *,char *,size_t);
  ssize_t (*write)(struct file *,const char *,size_t);
};
int register_driver(const char *,const struct file_operations *,mode_t,void *);
