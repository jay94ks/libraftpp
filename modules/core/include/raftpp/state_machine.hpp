#ifndef RAFTPP_STATE_MACHINE_HPP
#define RAFTPP_STATE_MACHINE_HPP

#include <raftpp/buffer.hpp>
#include <raftpp/error.hpp>

namespace raftpp {

    using apply_callback = function<void(error_code, shared<void>)>;

    class RAFTPP_API state_machine {
    public:
        virtual ~state_machine() = default;
        virtual error_code apply(view<const byte> command, shared<void>& result) = 0;
        virtual error_code restore(shared_buffer data) = 0;
        virtual error_code snapshot(vector<shared_buffer>& buffers) = 0;
        // Optional second snapshot phase, matching the original FSM hook. It
        // returns synchronously; this is not a coroutine or a future interface.
        virtual error_code snapshot_async(vector<shared_buffer>&) {
            return {};
        }
        virtual void snapshot_finalize(vector<shared_buffer>& buffers) noexcept {
            buffers.clear();
        }
    };

}

#endif // RAFTPP_STATE_MACHINE_HPP
