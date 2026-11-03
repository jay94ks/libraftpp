#ifndef RAFTPP_ZLIB_HPP
#define RAFTPP_ZLIB_HPP

#include <raftpp/compression.hpp>
#include <raftpp/zlib/export.hpp>

namespace raftpp {
    // Encodes one zlib-wrapped DEFLATE stream using the native SDK.
    class RAFTPP_ZLIB_API zlib_codec final : public compression_codec {
    public:
        error_code compress(view<const byte> input, buffer& output) const noexcept override;
        error_code decompress(view<const byte> input, buffer& output,
                              size_t max_size) const noexcept override;
    };
}
#endif
