#ifndef RAFTPP_SNAPSHOT_HPP
#define RAFTPP_SNAPSHOT_HPP

#include <raftpp/configuration.hpp>

namespace raftpp {

    struct snapshot {
        log_index index = 0;
        raftpp::term term = 0;
        raftpp::configuration configuration;
        log_index configuration_index = 0;
        vector<shared_buffer> buffers;
    };

    // Deep copy configuration and compact all payloads into one owned buffer.
    // Output remains unchanged on error; input and output may be the same object.
    [[nodiscard]] RAFTPP_API error_code copy_snapshot(const snapshot& input,
                                                      snapshot& output) noexcept;

}

#endif // RAFTPP_SNAPSHOT_HPP
