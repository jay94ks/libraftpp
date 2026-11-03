#ifndef RAFTPP_ZSTD_HPP
#define RAFTPP_ZSTD_HPP

#include <raftpp/compression.hpp>
#include <raftpp/zstd/export.hpp>

namespace raftpp {
    // Encodes one Zstandard frame using the native SDK.
    class RAFTPP_ZSTD_API zstd_codec final : public compression_codec {
    public:
        error_code compress(view<const byte> input, buffer& output) const noexcept override;
        error_code decompress(view<const byte> input, buffer& output,
                              size_t max_size) const noexcept override;
    };
}
#endif
