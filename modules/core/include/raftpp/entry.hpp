#ifndef RAFTPP_ENTRY_HPP
#define RAFTPP_ENTRY_HPP

#include <raftpp/buffer.hpp>
#include <raftpp/error.hpp>
#include <raftpp/types.hpp>

namespace raftpp {

    struct entry {
        raftpp::term term = 0;
        entry_type type = entry_type::command;
        shared_buffer payload;
    };

    // Deep copies. Output is unchanged on error, including when input aliases output.
    [[nodiscard]] RAFTPP_API error_code copy_entry(const entry& input, entry& output) noexcept;
    // All output payloads share one allocation, packed without padding as in entryBatchCopy.
    [[nodiscard]] RAFTPP_API error_code copy_entries(view<const entry> input,
                                                     vector<entry>& output) noexcept;

}

#endif // RAFTPP_ENTRY_HPP
