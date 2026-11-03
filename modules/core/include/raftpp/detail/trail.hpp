#ifndef RAFTPP_DETAIL_TRAIL_HPP
#define RAFTPP_DETAIL_TRAIL_HPP

#include <raftpp/error.hpp>
#include <raftpp/types.hpp>

namespace raftpp::detail {

    // Run-length encoded terms. Standard deque replaces the manual circular buffer.
    class RAFTPP_API trail {
    public:
        [[nodiscard]] error_code start(log_index snapshot_index, term snapshot_term,
                                       log_index start_index) noexcept;
        [[nodiscard]] error_code append(term term) noexcept;
        [[nodiscard]] error_code truncate(log_index index) noexcept;
        [[nodiscard]] error_code snapshot(log_index index, unsigned trailing) noexcept;
        [[nodiscard]] error_code restore(log_index index, term term) noexcept;

        [[nodiscard]] log_index size() const noexcept;
        [[nodiscard]] log_index last_index() const noexcept;
        [[nodiscard]] term last_term() const noexcept;
        [[nodiscard]] term term_of(log_index index) const noexcept;
        [[nodiscard]] bool has_entry(log_index index) const noexcept;
        [[nodiscard]] log_index snapshot_index() const noexcept {
            return snapshot_index_;
        }
        [[nodiscard]] term snapshot_term() const noexcept {
            return snapshot_term_;
        }

    private:
        struct record {
            log_index last_index;
            raftpp::term term;
        };

        resource_deque<record> records_;
        log_index offset_ = 0;
        log_index snapshot_index_ = 0;
        term snapshot_term_ = 0;
    };

}

#endif // RAFTPP_DETAIL_TRAIL_HPP
