// C++ port of cowsql/raft src/trail.c; see LICENSE.md.
#include <raftpp/detail/trail.hpp>

#include <algorithm>

namespace raftpp::detail {

    error_code trail::start(log_index snapshot_index, term snapshot_term,
                            log_index start_index) noexcept {
        if (!records_.empty() || start_index == 0 || start_index - 1 > snapshot_index ||
            (snapshot_index != 0 && snapshot_term == 0)) {
            return errc::invalid;
        }
        snapshot_index_ = snapshot_index;
        snapshot_term_ = snapshot_term;
        offset_ = start_index - 1;
        return {};
    }

    log_index trail::size() const noexcept {
        return records_.empty() ? 0 : records_.back().last_index - offset_;
    }

    log_index trail::last_index() const noexcept {
        return records_.empty() ? snapshot_index_ : records_.back().last_index;
    }

    term trail::last_term() const noexcept {
        return term_of(last_index());
    }

    bool trail::has_entry(log_index index) const noexcept {
        return !records_.empty() && index > offset_ && index <= records_.back().last_index;
    }

    term trail::term_of(log_index index) const noexcept {
        if (!has_entry(index)) {
            return index == snapshot_index_ ? snapshot_term_ : 0;
        }
        return std::ranges::lower_bound(records_, index, {}, &record::last_index)->term;
    }

    error_code trail::append(term term) noexcept {
        if (term == 0 || (!records_.empty() && term < records_.back().term)) {
            return errc::invalid;
        }
        const auto previous_index = records_.empty() ? offset_ : records_.back().last_index;
        if (previous_index == numeric_limits<log_index>::max()) {
            return errc::too_big;
        }
        if (!records_.empty() && records_.back().term == term) {
            records_.back().last_index = previous_index + 1;
            return {};
        }
        try {
            records_.push_back({previous_index + 1, term});
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code trail::truncate(log_index index) noexcept {
        if (!has_entry(index)) {
            return errc::invalid;
        }
        while (!records_.empty() && records_.back().last_index >= index) {
            const auto first =
                records_.size() == 1 ? offset_ + 1 : records_[records_.size() - 2].last_index + 1;
            if (index > first) {
                records_.back().last_index = index - 1;
                break;
            }
            records_.pop_back();
        }
        return {};
    }

    error_code trail::snapshot(log_index index, unsigned trailing) noexcept {
        const auto term = term_of(index);
        if (index == 0 || index == snapshot_index_ || term == 0) {
            return errc::invalid;
        }
        snapshot_index_ = index;
        snapshot_term_ = term;
        if (index <= trailing || term_of(index - trailing) == 0) {
            return {};
        }
        const auto cutoff = index - trailing;
        while (!records_.empty() && records_.front().last_index <= cutoff) {
            records_.pop_front();
        }
        offset_ = cutoff;
        return {};
    }

    error_code trail::restore(log_index index, term term) noexcept {
        if (index == 0 || term == 0) {
            return errc::invalid;
        }
        records_.clear();
        snapshot_index_ = offset_ = index;
        snapshot_term_ = term;
        return {};
    }

}
