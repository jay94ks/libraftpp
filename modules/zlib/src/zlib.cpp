#include <raftpp/zlib/zlib.hpp>
#include <zlib.h>
#include <algorithm>
#include <limits>

namespace raftpp {
    error_code zlib_codec::compress(view<const byte> input, buffer& output) const noexcept {
        if (input.size() > std::numeric_limits<uLong>::max())
            return errc::too_big;
        try {
            auto size = compressBound(static_cast<uLong>(input.size()));
            if (size < input.size())
                return errc::too_big;
            buffer encoded{size, output.get_allocator()};
            const byte empty{};
            const auto result =
                compress2(reinterpret_cast<Bytef*>(encoded.data()), &size,
                          reinterpret_cast<const Bytef*>(input.empty() ? &empty : input.data()),
                          static_cast<uLong>(input.size()), Z_DEFAULT_COMPRESSION);
            if (result == Z_MEM_ERROR)
                return errc::out_of_memory;
            if (result != Z_OK)
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

    error_code zlib_codec::decompress(view<const byte> input, buffer& output,
                                      size_t max_size) const noexcept {
        z_stream stream{};
        const auto initialized = inflateInit(&stream);
        if (initialized == Z_MEM_ERROR)
            return errc::out_of_memory;
        if (initialized != Z_OK)
            return errc::io_error;
        struct end_inflate {
            z_stream* stream;
            ~end_inflate() {
                inflateEnd(stream);
            }
        } end{&stream};
        try {
            buffer decoded{output.get_allocator()};
            array<byte, 65536> chunk{};
            size_t offset = 0;
            for (;;) {
                if (stream.avail_in == 0 && offset < input.size()) {
                    const auto count =
                        std::min(input.size() - offset,
                                 static_cast<size_t>(std::numeric_limits<uInt>::max()));
                    stream.next_in =
                        const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data() + offset));
                    stream.avail_in = static_cast<uInt>(count);
                    offset += count;
                }
                stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
                stream.avail_out = static_cast<uInt>(chunk.size());
                const auto before = stream.avail_in;
                const auto result = inflate(&stream, Z_NO_FLUSH);
                if (result == Z_MEM_ERROR)
                    return errc::out_of_memory;
                if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
                    return errc::corrupt;
                const auto count = chunk.size() - stream.avail_out;
                if (count > max_size - decoded.size())
                    return errc::too_big;
                const auto previous = decoded.size();
                decoded.resize(previous + count);
                if (count)
                    std::copy_n(chunk.data(), count, decoded.data() + previous);
                if (result == Z_STREAM_END) {
                    if (offset != input.size() || stream.avail_in != 0)
                        return errc::corrupt;
                    output.swap(decoded);
                    return {};
                }
                if (before == stream.avail_in && count == 0)
                    return errc::corrupt;
            }
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }
}
