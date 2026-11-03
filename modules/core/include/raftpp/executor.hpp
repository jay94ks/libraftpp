#ifndef RAFTPP_EXECUTOR_HPP
#define RAFTPP_EXECUTOR_HPP

#include <raftpp/error.hpp>

namespace raftpp {
    // Callback scheduling only; no coroutine or future API. A successful post
    // transfers the callable to the executor and runs it exactly once, later.
    // The owner must keep driving the underlying loop until its queue drains.
    class RAFTPP_API executor {
    public:
        virtual ~executor() = default;
        virtual error_code post(function<void()> callback) noexcept = 0;
    };
}
#endif
