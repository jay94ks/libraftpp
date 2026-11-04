#include <raftpp/uv/uv_storage.hpp>

namespace raftpp {
    uv_storage::uv_storage(uv_loop_s& loop, fs::path directory, uv_io_options options)
        : backend_(new uv_io(loop, std::move(directory), std::move(options))) {}
    uv_storage::~uv_storage() = default;
    error_code uv_storage::init(server_id id, string_view address) noexcept {
        return backend_->init(id, address);
    }
    error_code uv_storage::bootstrap(const configuration& value) noexcept {
        return backend_->bootstrap(value);
    }
    error_code uv_storage::recover(const configuration& value) noexcept {
        return backend_->recover(value);
    }
    error_code uv_storage::load(io_state& output) noexcept {
        return backend_->load(output);
    }
    error_code uv_storage::set_term(term value) noexcept {
        return backend_->set_term(value);
    }
    error_code uv_storage::set_vote(server_id value) noexcept {
        return backend_->set_vote(value);
    }
    error_code uv_storage::truncate(log_index index) noexcept {
        return backend_->truncate(index);
    }
    error_code uv_storage::append(log_index index, shared<const entry_batch> entries,
                                  completion callback) noexcept {
        return backend_->append(index, std::move(entries), std::move(callback));
    }
    error_code uv_storage::snapshot_put(unsigned trailing, shared<const snapshot> value,
                                        completion callback) noexcept {
        return backend_->snapshot_put(trailing, std::move(value), std::move(callback));
    }
    error_code uv_storage::snapshot_get(snapshot_callback callback) noexcept {
        return backend_->snapshot_get(std::move(callback));
    }
    void uv_storage::close(close_callback callback) noexcept {
        backend_->close(std::move(callback));
    }
    uint16_t uv_storage::capacity() const noexcept {
        return backend_->capacity();
    }
    error_info uv_storage::error_details() const noexcept {
        return backend_->error_details();
    }
}
