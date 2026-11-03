// Linux AIO syscall bindings derived from cowsql/raft syscall.c; see LICENSE.md.
#ifndef RAFTPP_SRC_AIO_HPP
#define RAFTPP_SRC_AIO_HPP

#include <cerrno>
#include <ctime>
#include <linux/aio_abi.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace raftpp::detail::native_aio {
    inline int setup(unsigned size, aio_context_t& context) noexcept {
        return syscall(SYS_io_setup, size, &context) < 0 ? -errno : 0;
    }
    inline int destroy(aio_context_t context) noexcept {
        long status;
        do {
            status = syscall(SYS_io_destroy, context);
        } while (status < 0 && errno == EINTR);
        return status < 0 ? -errno : 0;
    }
    inline int submit(aio_context_t context, iocb& request) noexcept {
        auto pointer = &request;
        const auto status = syscall(SYS_io_submit, context, 1L, &pointer);
        return status < 0 ? -errno : status == 1 ? 0 : -EAGAIN;
    }
    inline long get_events(aio_context_t context, long minimum, long maximum, io_event* events,
                           timespec* timeout) noexcept {
        long status;
        do {
            status = syscall(SYS_io_getevents, context, minimum, maximum, events, timeout);
        } while (status < 0 && errno == EINTR);
        return status < 0 ? -errno : status;
    }
}
#endif // RAFTPP_SRC_AIO_HPP
