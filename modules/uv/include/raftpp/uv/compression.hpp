#ifndef RAFTPP_UV_COMPRESSION_HPP
#define RAFTPP_UV_COMPRESSION_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/buffer.hpp>
#include <raftpp/config.hpp>
#include <raftpp/error.hpp>

namespace raftpp {

    [[nodiscard]] RAFTPP_UV_API bool is_compressed(view<const byte> input) noexcept;

    // Leaves output unchanged on failure. Without LZ4 this returns errc::invalid,
    // matching the original library. max_size bounds decoded bytes, not frame size.
    [[nodiscard]] RAFTPP_UV_API error_code
    decompress(view<const byte> input, buffer& output,
               size_t max_size = numeric_limits<size_t>::max()) noexcept;

}

#endif // RAFTPP_UV_COMPRESSION_HPP
