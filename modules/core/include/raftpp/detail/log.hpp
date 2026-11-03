#ifndef RAFTPP_DETAIL_LOG_HPP
#define RAFTPP_DETAIL_LOG_HPP

#include <raftpp/configuration.hpp>
#include <raftpp/entry.hpp>

namespace raftpp::detail {

    // Acquired entry handles retain headers and complete batches independently of the log.
    class RAFTPP_API log {
    public:
        log() = default;
        log(const log&) = delete;
        log& operator=(const log&) = delete;
        log(log&&) = delete;
        log& operator=(log&&) = delete;

        [[nodiscard]] error_code start(log_index snapshot_index, term snapshot_term,
                                       log_index start_index) noexcept;
        [[nodiscard]] error_code append(const entry& value) noexcept;
        // As in the C API, an error may follow a successfully appended prefix.
        [[nodiscard]] error_code append_commands(term term,
                                                 view<const shared_buffer> payloads) noexcept;
        [[nodiscard]] error_code append_configuration(term term,
                                                      const configuration& configuration) noexcept;
        [[nodiscard]] error_code
        acquire(log_index index, vector<shared<const entry>>& output,
                size_t maximum = numeric_limits<size_t>::max()) const noexcept;
        [[nodiscard]] error_code
        acquire(log_index index, resource_vector<shared<const entry>>& output,
                size_t maximum = numeric_limits<size_t>::max()) const noexcept;
        [[nodiscard]] error_code truncate(log_index index) noexcept;
        // Callers retain their own shared buffers; no raw ownership is relinquished.
        [[nodiscard]] error_code discard(log_index index) noexcept {
            return truncate(index);
        }
        [[nodiscard]] error_code snapshot(log_index index, unsigned trailing) noexcept;
        [[nodiscard]] error_code restore(log_index index, term term) noexcept;

        [[nodiscard]] size_t size() const noexcept {
            return entries_.size();
        }
        [[nodiscard]] bool empty() const noexcept {
            return entries_.empty();
        }
        [[nodiscard]] log_index first_index() const noexcept {
            return empty() ? 0 : offset_ + 1;
        }
        [[nodiscard]] log_index last_index() const noexcept {
            return offset_ + size();
        }
        [[nodiscard]] term last_term() const noexcept {
            return term_of(last_index());
        }
        [[nodiscard]] term term_of(log_index index) const noexcept;
        [[nodiscard]] borrowed<const entry> get(log_index index) const noexcept;
        [[nodiscard]] log_index snapshot_index() const noexcept {
            return snapshot_index_;
        }
        [[nodiscard]] term snapshot_term() const noexcept {
            return snapshot_term_;
        }

    private:
        void prune() noexcept;
        template <typename Allocator>
        error_code acquire_entries(log_index index, vector<shared<const entry>, Allocator>& output,
                                   size_t maximum) const noexcept;

        resource_deque<shared<const entry>> entries_;
        resource_map<pair<term, log_index>, weak<const entry>> references_;
        log_index offset_ = 0;
        log_index snapshot_index_ = 0;
        term snapshot_term_ = 0;
    };

}

#endif // RAFTPP_DETAIL_LOG_HPP
