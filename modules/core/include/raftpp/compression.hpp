#ifndef RAFTPP_COMPRESSION_HPP
#define RAFTPP_COMPRESSION_HPP

#include <raftpp/buffer.hpp>
#include <raftpp/error.hpp>

namespace raftpp {
    // One complete, self-contained frame. Decoders reject truncation, trailing
    // bytes and expansion above max_size. Both methods preserve output on error
    // (including when input aliases output) and use output's allocator.
    class RAFTPP_API compression_codec {
    public:
        virtual ~compression_codec() = default;
        virtual error_code compress(view<const byte> input, buffer& output) const noexcept = 0;
        virtual error_code decompress(view<const byte> input, buffer& output,
                                      size_t max_size) const noexcept = 0;
    };
}
#endif
