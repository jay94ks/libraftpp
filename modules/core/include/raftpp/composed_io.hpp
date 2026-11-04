#ifndef RAFTPP_COMPOSED_IO_HPP
#define RAFTPP_COMPOSED_IO_HPP

#include <raftpp/compression.hpp>
#include <raftpp/storage.hpp>
#include <raftpp/timer.hpp>
#include <raftpp/transport.hpp>

namespace raftpp {
    struct io_components {
        shared<raftpp::storage> storage{};
        shared<raftpp::timer> timer{};
        shared<transport> network{};
        shared<compression_codec> snapshot_codec{};
        size_t max_snapshot_size = 8 * 1024 * 1024;
        size_t max_encoded_snapshot_size = 16 * 1024 * 1024;
        uint32_t random_seed = 1;
    };

    // Exclusively controls each component's init/start/close lifecycle. All
    // components must already share one serialized domain; no implicit bridge.
    // Missing required components throw invalid_argument. A selected codec uses
    // a versioned snapshot-payload envelope; RPC/log formats remain unchanged.
    [[nodiscard]] RAFTPP_API shared<io> make_io(io_components components);
}
#endif
