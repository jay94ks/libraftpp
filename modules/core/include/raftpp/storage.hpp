#ifndef RAFTPP_STORAGE_HPP
#define RAFTPP_STORAGE_HPP

#include <raftpp/io.hpp>

namespace raftpp {
    struct storage_limits {
        size_t max_entry_size = 8 * 1024 * 1024;
        size_t max_snapshot_size = 16 * 1024 * 1024;
        size_t max_entries = 262144;
    };

    // All calls/completions use the composed I/O's serialized execution domain.
    // Accepted requests retain their data and drain before close completes.
    // Snapshot payloads are opaque bytes; their encoding belongs to the composer.
    class RAFTPP_API storage {
    public:
        virtual ~storage() = default;
        virtual error_code init(server_id id, string_view address) = 0;
        virtual error_code bootstrap(const configuration& value) = 0;
        virtual error_code recover(const configuration& value) = 0;
        virtual error_code load(io_state& output) = 0;
        virtual error_code set_term(term value) = 0;
        virtual error_code set_vote(server_id value) = 0;
        virtual error_code truncate(log_index index) = 0;
        virtual error_code append(log_index index, shared<const entry_batch> entries,
                                  completion callback) = 0;
        virtual error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                        completion callback) = 0;
        virtual error_code snapshot_get(snapshot_callback callback) = 0;
        virtual void close(close_callback callback) noexcept = 0;
        [[nodiscard]] virtual uint16_t capacity() const noexcept {
            return 0;
        }
        [[nodiscard]] virtual error_info error_details() const noexcept {
            return {};
        }
    };
}
#endif
