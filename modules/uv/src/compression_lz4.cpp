#include <raftpp/uv/compression.hpp>

#include <algorithm>

#include <lz4frame.h>

namespace raftpp {

    error_code decompress(view<const byte> input, buffer& output, size_t max_size) noexcept {
        if (input.empty()) {
            return errc::io_error;
        }
        LZ4F_dctx* native = nullptr;
        if (LZ4F_isError(LZ4F_createDecompressionContext(&native, LZ4F_VERSION))) {
            return errc::out_of_memory;
        }
        owned<LZ4F_dctx, decltype(&LZ4F_freeDecompressionContext)> context{
            native, &LZ4F_freeDecompressionContext};

        try {
            buffer decoded;
            array<byte, 64 * 1024> chunk;
            size_t offset = 0;
            for (;;) {
                size_t source_size = input.size() - offset;
                size_t destination_size = std::min(chunk.size(), max_size - decoded.size());
                const auto source = input.empty() ? nullptr : input.data() + offset;
                const auto hint = LZ4F_decompress(context.get(), chunk.data(), &destination_size,
                                                  source, &source_size, nullptr);
                if (LZ4F_isError(hint)) {
                    return errc::io_error;
                }
                offset += source_size;
                if (destination_size != 0) {
                    const auto previous_size = decoded.size();
                    decoded.resize(previous_size + destination_size);
                    std::copy_n(chunk.begin(), destination_size, decoded.data() + previous_size);
                }
                if (hint == 0) {
                    output.swap(decoded);
                    return {};
                }
                if (source_size == 0 && destination_size == 0) {
                    return decoded.size() == max_size ? errc::too_big : errc::io_error;
                }
            }
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

}
