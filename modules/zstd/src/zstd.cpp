#include <raftpp/zstd/zstd.hpp>
#include <zstd.h>
#include <algorithm>
#include <limits>

namespace raftpp {
    error_code zstd_codec::compress(view<const byte> input, buffer& output) const noexcept {
        try {
            const auto bound = ZSTD_compressBound(input.size());
            if (ZSTD_isError(bound))
                return errc::too_big;
            buffer encoded{bound, output.get_allocator()};
            const auto size = ZSTD_compress(encoded.data(), bound, input.data(), input.size(), 3);
            if (ZSTD_isError(size))
                return errc::io_error;
            encoded.resize(size);
            output.swap(encoded);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code zstd_codec::decompress(view<const byte> input, buffer& output,
                                      size_t max_size) const noexcept {
        owned<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context{ZSTD_createDCtx(), &ZSTD_freeDCtx};
        if (!context)
            return errc::out_of_memory;
        try {
            buffer decoded{output.get_allocator()};
            array<byte, 65536> chunk{};
            ZSTD_inBuffer source{input.data(), input.size(), 0};
            for (;;) {
                ZSTD_outBuffer destination{chunk.data(), chunk.size(), 0};
                const auto before = source.pos;
                const auto hint = ZSTD_decompressStream(context.get(), &destination, &source);
                if (ZSTD_isError(hint))
                    return errc::corrupt;
                if (destination.pos > max_size - decoded.size())
                    return errc::too_big;
                const auto previous = decoded.size();
                decoded.resize(previous + destination.pos);
                if (destination.pos)
                    std::copy_n(chunk.data(), destination.pos, decoded.data() + previous);
                if (hint == 0) {
                    if (source.pos != source.size)
                        return errc::corrupt;
                    output.swap(decoded);
                    return {};
                }
                if (source.pos == before && destination.pos == 0)
                    return errc::corrupt;
            }
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }
}
