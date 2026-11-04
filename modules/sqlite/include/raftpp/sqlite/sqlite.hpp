#ifndef RAFTPP_SQLITE_HPP
#define RAFTPP_SQLITE_HPP
#include <raftpp/storage.hpp>
#include <raftpp/sqlite/export.hpp>

namespace raftpp {
    namespace detail {
        struct database_state;
    }
    // Serialized, blocking DB calls. Successful requests complete inline after
    // durable commit; close waits for active callbacks. A dedicated DB is owned.
    class RAFTPP_SQLITE_API sqlite_storage final : public storage {
    public:
        explicit sqlite_storage(fs::path path, storage_limits limits = {});
        ~sqlite_storage() override;
        sqlite_storage(const sqlite_storage&) = delete;
        sqlite_storage& operator=(const sqlite_storage&) = delete;
        error_code init(server_id id, string_view address) noexcept override;
        error_code bootstrap(const configuration& value) noexcept override;
        error_code recover(const configuration& value) noexcept override;
        error_code load(io_state& output) noexcept override;
        error_code set_term(term value) noexcept override;
        error_code set_vote(server_id value) noexcept override;
        error_code truncate(log_index index) noexcept override;
        error_code append(log_index index, shared<const entry_batch> entries,
                          completion callback) noexcept override;
        error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                completion callback) noexcept override;
        error_code snapshot_get(snapshot_callback callback) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        uint16_t capacity() const noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::database_state> state_;
    };
}
#endif
