#ifndef RAFTPP_LZ4_HPP
#define RAFTPP_LZ4_HPP

#include <raftpp/compression.hpp>
#include <raftpp/lz4/export.hpp>

namespace raftpp {
    // Encodes one LZ4 frame using the native SDK.
    class RAFTPP_LZ4_API lz4_codec final : public compression_codec {
    public:
        error_code compress(view<const byte> input, buffer& output) const noexcept override;
        error_code decompress(view<const byte> input, buffer& output,
                              size_t max_size) const noexcept override;
    };
}
#endif
