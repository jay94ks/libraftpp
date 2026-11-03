// C++ port of cowsql/raft src/snapshot.c; see LICENSE.md.
#include <raftpp/snapshot.hpp>

#include <algorithm>

namespace raftpp {

    error_code copy_snapshot(const snapshot& input, snapshot& output) noexcept {
        size_t size = 0;
        for (const auto& buffer : input.buffers) {
            if (buffer.size() > numeric_limits<size_t>::max() - size) {
                return errc::too_big;
            }
            size += buffer.size();
        }
        try {
            snapshot copied;
            copied.index = input.index;
            copied.term = input.term;
            copied.configuration_index = input.configuration_index;
            copied.configuration = input.configuration;
            buffer storage{size};
            size_t offset = 0;
            for (const auto& buffer : input.buffers) {
                if (!buffer.empty()) {
                    std::ranges::copy(buffer.bytes(), storage.data() + offset);
                }
                offset += buffer.size();
            }
            copied.buffers.emplace_back(std::move(storage));
            output = std::move(copied);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

}
