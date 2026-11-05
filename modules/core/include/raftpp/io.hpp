#ifndef RAFTPP_IO_HPP
#define RAFTPP_IO_HPP

#include <raftpp/message.hpp>
#include <raftpp/snapshot.hpp>

namespace raftpp {

    using completion = function<void(error_code)>;
    using close_callback = function<void()>;
    using snapshot_callback = function<void(error_code, optional<snapshot>)>;

    struct io_state {
        raftpp::term term = 0;
        server_id voted_for = 0;
        optional<raftpp::snapshot> snapshot;
        log_index start_index = 1;
        vector<entry> entries;
    };

    // Calls and callbacks are serialized on one event-loop thread. Accepted
    // async operations complete exactly once, including during close. An
    // immediate error means the completion will not run. Inline completion is
    // allowed. close stops subscriptions and drains accepted operations first.
    class RAFTPP_API io {
    public:
        virtual ~io() = default;
        virtual error_code init(server_id id, string_view address) = 0;
        virtual error_code bootstrap(const configuration& configuration) = 0;
        virtual error_code recover(const configuration& configuration) = 0;
        virtual error_code load(io_state& state) = 0;
        virtual error_code start(unsigned interval, function<void()> tick,
                                 function<void(message)> receive) = 0;
        virtual void close(close_callback callback) noexcept = 0;
        virtual error_code set_term(term term) = 0;
        virtual error_code set_vote(server_id id) = 0;
        virtual error_code truncate(log_index index) = 0;
        virtual error_code append(log_index index, shared<const entry_batch> entries,
                                  completion callback) = 0;
        virtual error_code send(shared<const message> message, completion callback) = 0;
        virtual error_code snapshot_put(unsigned trailing, shared<const snapshot> snapshot,
                                        completion callback) = 0;
        // A locally created snapshot preserves any newer accepted log suffix.
        // The default forwards to snapshot_put for backends without a distinct
        // installation path. UV overrides this to distinguish trailing=0.
        virtual error_code snapshot_put_local(unsigned trailing, shared<const snapshot> value,
                                              completion callback) {
            return snapshot_put(trailing, std::move(value), std::move(callback));
        }
        virtual error_code snapshot_get(snapshot_callback callback) = 0;
        [[nodiscard]] virtual uint64_t time() const noexcept = 0;
        [[nodiscard]] virtual uint32_t random(uint32_t minimum, uint32_t maximum) noexcept = 0;
        [[nodiscard]] virtual uint16_t capacity() const noexcept {
            return 0;
        } // KiB
        // Optional owning diagnostics. Publish before returning/completing failure.
        [[nodiscard]] virtual error_info error_details() const noexcept {
            return {};
        }
    };

}

#endif // RAFTPP_IO_HPP
