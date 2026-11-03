// C++ port of cowsql/raft src/uv_metadata.c and uv_snapshot.c; see LICENSE.md.
#include <raftpp/encoding.hpp>
#include <raftpp/checksum.hpp>
#include "byte.hpp"

namespace raftpp {

    error_code encode_metadata(const persistent_metadata& input, buffer& output) noexcept {
        if (input.version == 0)
            return errc::corrupt;
        try {
            buffer result{32};
            detail::byte_writer writer{result.bytes()};
            writer.write<uint64_t>(disk_format_version);
            writer.write(input.version);
            writer.write(input.term);
            writer.write(input.voted_for);
            output = std::move(result);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        }
    }

    error_code decode_metadata(view<const byte> input, persistent_metadata& output) noexcept {
        if (input.size() < 32)
            return errc::io_error;
        if (input.size() > 32)
            return errc::corrupt;
        detail::byte_reader reader{input};
        uint64_t format = 0;
        persistent_metadata result;
        (void)reader.read(format);
        if (format != disk_format_version)
            return errc::malformed;
        (void)reader.read(result.version);
        (void)reader.read(result.term);
        (void)reader.read(result.voted_for);
        if (result.version == 0)
            return errc::corrupt;
        output = result;
        return {};
    }

    error_code encode_snapshot_metadata(const snapshot& input, buffer& output) noexcept {
        try {
            const auto size = input.configuration.encoded_size();
            if (size == 0 || size > 1024 * 1024)
                return errc::too_big;
            buffer result{32 + size};
            detail::byte_writer writer{result.bytes()};
            writer.write<uint64_t>(disk_format_version);
            writer.write<uint64_t>(0);
            writer.write(input.configuration_index);
            writer.write<uint64_t>(size);
            if (auto error = input.configuration.encode(result.bytes().subspan(32)))
                return error;
            detail::store_little_endian<uint64_t>(crc32(result.bytes().subspan(16)),
                                                  result.bytes().subspan<8, 8>());
            output = std::move(result);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code decode_snapshot_metadata(view<const byte> input, snapshot& output) noexcept {
        if (input.size() < 32)
            return errc::io_error;
        detail::byte_reader reader{input};
        uint64_t format = 0, checksum = 0, index = 0, size = 0;
        (void)reader.read(format);
        (void)reader.read(checksum);
        (void)reader.read(index);
        (void)reader.read(size);
        if (format != disk_format_version)
            return errc::malformed;
        if (size == 0 || size > 1024 * 1024)
            return errc::corrupt;
        if (size > input.size() - 32)
            return errc::io_error;
        if (static_cast<uint32_t>(checksum) !=
            crc32(input.subspan(16, 16 + static_cast<size_t>(size))))
            return errc::corrupt;
        configuration configuration;
        if (auto error = configuration.decode(input.subspan(32, static_cast<size_t>(size))))
            return error;
        output.configuration = std::move(configuration);
        output.configuration_index = index;
        return {};
    }

}
