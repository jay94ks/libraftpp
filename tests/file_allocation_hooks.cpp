/* Test-only syscall interposition. Unarmed calls always reach libc; armed
 * calls inject allocation capability/errors but perform real fallback writes. */
#undef _FILE_OFFSET_BITS
#define _GNU_SOURCE 1
#include "file_allocation.hpp"
#include <fcntl.h>

#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <sys/vfs.h>
#include <unistd.h>

static thread_local int armed;
static thread_local int watched = -1;
static thread_local struct raftpp_allocation_plan plan;
static thread_local struct raftpp_allocation_trace trace;

void raftpp_allocation_begin(const struct raftpp_allocation_plan* value) {
    plan = *value;
    memset(&trace, 0, sizeof trace);
    watched = -1;
    armed = 1;
}
void raftpp_allocation_end(struct raftpp_allocation_trace* output) {
    armed = 0;
    *output = trace;
}

/* POSIX permits dlsym of functions. Copy the pointer representation without a
 * non-ISO object-to-function pointer cast in this C11 test translation unit. */
#define RAFTPP_NATIVE_FUNCTION(type, symbol)                                                       \
    type actual;                                                                                   \
    void* address = dlsym(RTLD_NEXT, symbol);                                                      \
    static_assert(sizeof actual == sizeof address, "function pointer size");                       \
    memcpy(&actual, &address, sizeof actual)

static int allocation(int fd) {
    if (!armed)
        return 0;
    watched = fd;
    ++trace.allocations;
    /* posix_fallocate returns its error; errno must not determine the result. */
    errno = EACCES;
    return plan.allocation_error;
}
int posix_fallocate(int fd, off_t offset, off_t length) {
    const int status = allocation(fd);
    if (status)
        return status;
    typedef int (*native_function)(int, off_t, off_t);
    RAFTPP_NATIVE_FUNCTION(native_function, "posix_fallocate");
    return actual ? actual(fd, offset, length) : EIO;
}
#ifndef posix_fallocate64
int posix_fallocate64(int fd, off64_t offset, off64_t length) {
    const int status = allocation(fd);
    if (status)
        return status;
    typedef int (*native_function)(int, off64_t, off64_t);
    RAFTPP_NATIVE_FUNCTION(native_function, "posix_fallocate64");
    return actual ? actual(fd, offset, length) : EIO;
}
#endif
static int query(int fd) {
    if (!armed || watched != fd)
        return 0;
    ++trace.queries;
    if (plan.stat_error) {
        errno = plan.stat_error;
        return -1;
    }
    return 0;
}
int fstatfs(int fd, struct statfs* output) noexcept {
    if (query(fd))
        return -1;
    typedef int (*native_function)(int, struct statfs*);
    RAFTPP_NATIVE_FUNCTION(native_function, "fstatfs");
    if (!actual) {
        errno = ENOSYS;
        return -1;
    }
    const int status = actual(fd, output);
    if (status == 0 && armed && watched == fd && plan.block_size >= 0)
        output->f_bsize = plan.block_size;
    return status;
}
#ifndef fstatfs64
int fstatfs64(int fd, struct statfs64* output) noexcept {
    if (query(fd))
        return -1;
    typedef int (*native_function)(int, struct statfs64*);
    RAFTPP_NATIVE_FUNCTION(native_function, "fstatfs64");
    if (!actual) {
        errno = ENOSYS;
        return -1;
    }
    const int status = actual(fd, output);
    if (status == 0 && armed && watched == fd && plan.block_size >= 0)
        output->f_bsize = plan.block_size;
    return status;
}
#endif
static int writing(int fd, uint64_t offset) {
    if (!armed || watched != fd)
        return 0;
    const unsigned index = trace.writes++;
    if (index < 64)
        trace.positions[index] = offset;
    if (plan.write_error && (int)index == plan.write_after) {
        errno = plan.write_error;
        return -1;
    }
    return 0;
}
ssize_t pwrite(int fd, const void* data, size_t size, off_t offset) {
    if (writing(fd, (uint64_t)offset))
        return -1;
    typedef ssize_t (*native_function)(int, const void*, size_t, off_t);
    RAFTPP_NATIVE_FUNCTION(native_function, "pwrite");
    if (!actual) {
        errno = ENOSYS;
        return -1;
    }
    return actual(fd, data, size, offset);
}
#ifndef pwrite64
ssize_t pwrite64(int fd, const void* data, size_t size, off64_t offset) {
    if (writing(fd, (uint64_t)offset))
        return -1;
    typedef ssize_t (*native_function)(int, const void*, size_t, off64_t);
    RAFTPP_NATIVE_FUNCTION(native_function, "pwrite64");
    if (!actual) {
        errno = ENOSYS;
        return -1;
    }
    return actual(fd, data, size, offset);
}
#endif
