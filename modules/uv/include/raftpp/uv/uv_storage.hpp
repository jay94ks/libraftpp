#ifndef RAFTPP_UV_STORAGE_HPP
#define RAFTPP_UV_STORAGE_HPP

#include <raftpp/storage.hpp>
#include <raftpp/uv/uv_io.hpp>

namespace raftpp {
    // Durable UV files without a network or a periodic node timer. Existing
    // log/snapshot formats, barriers and disk-retry policy are preserved.
    class RAFTPP_UV_API uv_storage final : public storage {
    public:
        uv_storage(uv_loop_s& loop, fs::path directory, uv_io_options options = {});
        ~uv_storage() override;
        uv_storage(const uv_storage&) = delete;
        uv_storage& operator=(const uv_storage&) = delete;
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
        owned<uv_io> backend_;
    };
}
#endif
