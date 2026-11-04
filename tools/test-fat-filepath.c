/* SPDX-License-Identifier: Apache-2.0
 * Unit-test the actual extracted FAT ioctl with mock VFS/mount primitives.
 * Does not substitute for SD/UnQLite persistence and locking on hardware.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FAR
#define DEBUGASSERT assert
#define PATH_MAX 128
#define OK 0
#define FIOC_FILEPATH 42
#define UMOUNT_FORCED 0x80
struct fat_mountpt_s { int fs_lock; };
struct fat_file_s { int ff_bflags; const char *ff_relpath; };
struct inode { struct fat_mountpt_s *i_private; const char *path; };
struct file { struct inode *f_inode; struct fat_file_s *f_priv; };
static int mount_status;
static int nxmutex_lock(int *lock) { assert(!*lock); *lock = 1; return 0; }
static void nxmutex_unlock(int *lock) { assert(*lock); *lock = 0; }
static int fat_checkmount(struct fat_mountpt_s *fs)
{ assert(fs->fs_lock); return mount_status; }
static int inode_getpath(struct inode *inode, char *out, size_t capacity)
{
    size_t n = strlen(inode->path);
    if (n >= capacity)
        return -ENAMETOOLONG;
    memcpy(out, inode->path, n + 1);
    return 0;
}
#include "fat-filepath-extracted.h"
int main(void)
{
    struct fat_mountpt_s fs = {0};
    struct fat_file_s ff = {0, "persist.db"};
    struct inode inode = {&fs, "/data"};
    struct file file = {&inode, &ff};
    char path[PATH_MAX];
    assert(fat_ioctl(&file, FIOC_FILEPATH, (uintptr_t)path) == 0);
    assert(!strcmp(path, "/data/persist.db"));
    inode.path = "/";
    assert(fat_ioctl(&file, FIOC_FILEPATH, (uintptr_t)path) == 0);
    assert(!strcmp(path, "/persist.db"));
    inode.path = "/data/";
    ff.ff_relpath = "misc/bt/settings.db";
    assert(fat_ioctl(&file, FIOC_FILEPATH, (uintptr_t)path) == 0);
    assert(!strcmp(path, "/data/misc/bt/settings.db"));
    assert(fat_ioctl(&file, FIOC_FILEPATH, 0) == -EINVAL);
    assert(fat_ioctl(&file, 123, (uintptr_t)path) == -ENOTTY);
    mount_status = -ENODEV;
    assert(fat_ioctl(&file, FIOC_FILEPATH, (uintptr_t)path) == -ENODEV);
    mount_status = 0;
    ff.ff_bflags = UMOUNT_FORCED;
    assert(fat_ioctl(&file, FIOC_FILEPATH, (uintptr_t)path) == -EPIPE);
    ff.ff_bflags = 0;
    char long_name[PATH_MAX + 4];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    ff.ff_relpath = long_name;
    assert(fat_ioctl(&file, FIOC_FILEPATH, (uintptr_t)path) == -ENAMETOOLONG);
    assert(fs.fs_lock == 0);
    puts("FAT descriptor path/null/limit/mount/lock tests: PASS");
    return 0;
}
