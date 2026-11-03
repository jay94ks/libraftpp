// C++ port of cowsql/raft src/entry.c; see LICENSE.md.
#include <raftpp/entry.hpp>

#include <algorithm>

namespace raftpp {

    error_code copy_entry(const entry& input, entry& output) noexcept {
        try {
            entry copied{input.term, input.type, shared_buffer::copy(input.payload.bytes())};
            output = std::move(copied);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code copy_entries(view<const entry> input, vector<entry>& output) noexcept {
        size_t size = 0;
        for (const auto& entry : input) {
            if (entry.payload.size() > numeric_limits<size_t>::max() - size) {
                return errc::too_big;
            }
            size += entry.payload.size();
        }
        try {
            vector<entry> copied;
            copied.reserve(input.size());
            if (!input.empty()) {
                buffer storage{size};
                size_t offset = 0;
                for (const auto& entry : input) {
                    if (!entry.payload.empty()) {
                        std::ranges::copy(entry.payload.bytes(), storage.data() + offset);
                    }
                    offset += entry.payload.size();
                }
                shared_buffer batch{std::move(storage)};
                offset = 0;
                for (const auto& entry : input) {
                    copied.push_back(
                        {entry.term, entry.type, batch.slice(offset, entry.payload.size())});
                    offset += entry.payload.size();
                }
            }
            output.swap(copied);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

}
