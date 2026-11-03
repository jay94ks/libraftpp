/* Test-only interposition of the native allocation error, for both static and
 * shared C++ libraries. Unarmed calls go to the actual libc implementation. */
#undef _FILE_OFFSET_BITS
#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <fcntl.h>

static thread_local int no_space;
extern "C" void raftpp_test_no_space(int enabled) {
    no_space = enabled;
}

int posix_fallocate(int fd, off_t offset, off_t length) {
    typedef int (*allocate_fn)(int, off_t, off_t);
    allocate_fn actual = reinterpret_cast<allocate_fn>(dlsym(RTLD_NEXT, "posix_fallocate"));
    if (no_space)
        return ENOSPC;
    return actual != NULL ? actual(fd, offset, length) : EIO;
}
int posix_fallocate64(int fd, off64_t offset, off64_t length) {
    typedef int (*allocate_fn)(int, off64_t, off64_t);
    allocate_fn actual = reinterpret_cast<allocate_fn>(dlsym(RTLD_NEXT, "posix_fallocate64"));
    if (no_space)
        return ENOSPC;
    return actual != NULL ? actual(fd, offset, length) : EIO;
}
