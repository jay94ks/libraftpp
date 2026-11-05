#ifndef RAFTPP_UV_IO_HPP
#define RAFTPP_UV_IO_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/config.hpp>
#include <raftpp/transport.hpp>
#include <raftpp/tracing.hpp>

#if RAFTPP_WITH_UV
struct uv_loop_s;
namespace raftpp {
    namespace detail {
        struct uv_io_state;
    }
    struct uv_io_options {
        size_t segment_size = 8 * 1024 * 1024;
        unsigned disk_retry = 5000;
        size_t block_size = 0; // Zero keeps the filesystem probe's alignment.
        bool auto_recovery = true;
        size_t max_snapshot_size = numeric_limits<size_t>::max();
        tracer tracing{};
    };
    // Native durable storage and libuv timers with a pluggable message transport.
    // The loop must outlive asynchronous close, including after init/load failure.
    class RAFTPP_UV_API uv_io final : public io {
    public:
        uv_io(uv_loop_s& loop, fs::path directory, shared<transport> network,
              uv_io_options options = {});
        ~uv_io() override;
        uv_io(const uv_io&) = delete;
        uv_io& operator=(const uv_io&) = delete;
        error_code init(server_id id, string_view address) noexcept override;
        error_code bootstrap(const configuration& configuration) noexcept override;
        error_code recover(const configuration& configuration) noexcept override;
        error_code load(io_state& state) noexcept override;
        error_code start(unsigned interval, function<void()> tick,
                         function<void(message)> receive) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        error_code set_term(term term) noexcept override;
        error_code set_vote(server_id id) noexcept override;
        error_code truncate(log_index index) noexcept override;
        error_code append(log_index index, shared<const entry_batch> entries,
                          completion callback) noexcept override;
        error_code send(shared<const message> value, completion callback) noexcept override;
        error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                completion callback) noexcept override;
        error_code snapshot_put_local(unsigned trailing, shared<const snapshot> value,
                                      completion callback) noexcept override;
        error_code snapshot_get(snapshot_callback callback) noexcept override;
        [[nodiscard]] uint64_t time() const noexcept override;
        [[nodiscard]] uint32_t random(uint32_t minimum, uint32_t maximum) noexcept override;
        [[nodiscard]] uint16_t capacity() const noexcept override;
        [[nodiscard]] error_code last_error() const noexcept;
        [[nodiscard]] error_info error_details() const noexcept override;
        void set_tracer(tracer value) noexcept;
        [[nodiscard]] error_code set_block_size(size_t size) noexcept;
        [[nodiscard]] error_code set_segment_size(size_t size) noexcept;
        void set_disk_retry(unsigned msecs) noexcept;
        void set_connect_retry_delay(unsigned msecs) noexcept;
        void set_auto_recovery(bool enabled) noexcept;
        // Deprecated C compatibility setting: compression selection is a no-op.
        [[nodiscard]] error_code set_snapshot_compression(bool enabled) noexcept;

    private:
        uv_io(uv_loop_s& loop, fs::path directory, uv_io_options options);
        friend class uv_storage;
        shared<detail::uv_io_state> state_;
    };
}
#endif
#endif // RAFTPP_UV_IO_HPP
