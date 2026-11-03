// C++ port of cowsql/raft src/log.c; see LICENSE.md.
#include <raftpp/detail/log.hpp>

#include <algorithm>

namespace raftpp::detail {

    void log::prune() noexcept {
        std::erase_if(references_, [](const auto& item) { return item.second.expired(); });
    }

    error_code log::start(log_index snapshot_index, term snapshot_term,
                          log_index start_index) noexcept {
        if (!empty() || start_index == 0 || start_index - 1 > snapshot_index ||
            (snapshot_index != 0 && snapshot_term == 0)) {
            return errc::invalid;
        }
        snapshot_index_ = snapshot_index;
        snapshot_term_ = snapshot_term;
        offset_ = start_index - 1;
        prune();
        return {};
    }

    borrowed<const entry> log::get(log_index index) const noexcept {
        if (index <= offset_ || index > last_index()) {
            return nullptr;
        }
        return entries_[static_cast<size_t>(index - offset_ - 1)].get();
    }

    term log::term_of(log_index index) const noexcept {
        if (index == 0 || index > last_index()) {
            return 0;
        }
        if (index == snapshot_index_) {
            return snapshot_term_;
        }
        const auto entry = get(index);
        return entry == nullptr ? 0 : entry->term;
    }

    error_code log::append(const entry& value) noexcept {
        if (value.term == 0 ||
            (value.type != entry_type::command && value.type != entry_type::barrier &&
             value.type != entry_type::change)) {
            return errc::invalid;
        }
        if (last_index() == numeric_limits<log_index>::max()) {
            return errc::too_big;
        }
        const pair key{value.term, last_index() + 1};
        if (const auto found = references_.find(key); found != references_.end()) {
            if (!found->second.expired()) {
                return errc::busy;
            }
            references_.erase(found);
        }
        try {
            // A distinct header identifies each (term,index), even if payloads share storage.
            shared<const entry> stored =
                std::allocate_shared<const entry>(resource_allocator<entry>{}, value);
            const auto position = references_.emplace(key, stored).first;
            try {
                entries_.push_back(std::move(stored));
            } catch (...) {
                references_.erase(position);
                throw;
            }
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code log::append_commands(term term, view<const shared_buffer> payloads) noexcept {
        if (payloads.empty()) {
            return errc::invalid;
        }
        for (const auto& payload : payloads) {
            if (auto error = append({term, entry_type::command, payload})) {
                return error;
            }
        }
        return {};
    }

    error_code log::append_configuration(term term, const configuration& configuration) noexcept {
        buffer encoded;
        if (auto error = configuration.encode(encoded)) {
            return error;
        }
        try {
            return append({term, entry_type::change, shared_buffer{std::move(encoded)}});
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    template <typename Allocator>
    error_code log::acquire_entries(log_index index, vector<shared<const entry>, Allocator>& output,
                                    size_t maximum) const noexcept {
        if (index == 0) {
            return errc::invalid;
        }
        try {
            vector<shared<const entry>, Allocator> acquired{output.get_allocator()};
            if (get(index) != nullptr && maximum != 0) {
                const auto first = static_cast<size_t>(index - offset_ - 1);
                const auto count = std::min(size() - first, maximum);
                acquired.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    acquired.push_back(entries_[first + i]);
                }
            }
            output.swap(acquired);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code log::acquire(log_index index, vector<shared<const entry>>& output,
                            size_t maximum) const noexcept {
        return acquire_entries(index, output, maximum);
    }
    error_code log::acquire(log_index index, resource_vector<shared<const entry>>& output,
                            size_t maximum) const noexcept {
        return acquire_entries(index, output, maximum);
    }

    error_code log::truncate(log_index index) noexcept {
        if (empty()) {
            return {};
        }
        if (get(index) == nullptr) {
            return errc::invalid;
        }
        const auto remaining = static_cast<size_t>(index - offset_ - 1);
        while (entries_.size() > remaining) {
            entries_.pop_back();
        }
        prune();
        return {};
    }

    error_code log::snapshot(log_index index, unsigned trailing) noexcept {
        const auto term = term_of(index);
        if (index == 0 || term == 0) {
            return errc::invalid;
        }
        snapshot_index_ = index;
        snapshot_term_ = term;
        if (index > trailing && get(index - trailing) != nullptr) {
            const auto cutoff = index - trailing;
            while (offset_ < cutoff) {
                entries_.pop_front();
                ++offset_;
            }
            prune();
        }
        return {};
    }

    error_code log::restore(log_index index, term term) noexcept {
        if (index == 0 || term == 0) {
            return errc::invalid;
        }
        entries_.clear();
        offset_ = snapshot_index_ = index;
        snapshot_term_ = term;
        prune();
        return {};
    }

}
