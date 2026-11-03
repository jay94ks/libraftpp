#include <raftpp/lz4/lz4.hpp>
#include <lz4frame.h>
#include <algorithm>
#include <limits>

namespace raftpp {
    error_code lz4_codec::compress(view<const byte> input, buffer& output) const noexcept {
        try {
            LZ4F_preferences_t preferences{};
            preferences.frameInfo.contentSize = input.size();
            preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
            const auto bound = LZ4F_compressFrameBound(input.size(), &preferences);
            if (LZ4F_isError(bound))
                return errc::too_big;
            buffer encoded{bound, output.get_allocator()};
            const byte empty{};
            const auto size =
                LZ4F_compressFrame(encoded.data(), bound, input.empty() ? &empty : input.data(),
                                   input.size(), &preferences);
            if (LZ4F_isError(size))
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

    error_code lz4_codec::decompress(view<const byte> input, buffer& output,
                                     size_t max_size) const noexcept {
        LZ4F_dctx* native = nullptr;
        if (LZ4F_isError(LZ4F_createDecompressionContext(&native, LZ4F_VERSION)))
            return errc::out_of_memory;
        owned<LZ4F_dctx, decltype(&LZ4F_freeDecompressionContext)> context{
            native, &LZ4F_freeDecompressionContext};
        try {
            buffer decoded{output.get_allocator()};
            array<byte, 65536> chunk{};
            size_t offset = 0;
            for (;;) {
                size_t source_size = input.size() - offset;
                size_t destination_size = chunk.size();
                const auto hint = LZ4F_decompress(context.get(), chunk.data(), &destination_size,
                                                  source_size ? input.data() + offset : nullptr,
                                                  &source_size, nullptr);
                if (LZ4F_isError(hint))
                    return errc::corrupt;
                offset += source_size;
                if (destination_size > max_size - decoded.size())
                    return errc::too_big;
                const auto previous = decoded.size();
                decoded.resize(previous + destination_size);
                if (destination_size)
                    std::copy_n(chunk.data(), destination_size, decoded.data() + previous);
                if (hint == 0) {
                    if (offset != input.size())
                        return errc::corrupt;
                    output.swap(decoded);
                    return {};
                }
                if (source_size == 0 && destination_size == 0)
                    return errc::corrupt;
            }
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }
}
