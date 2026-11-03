// C++ port of cowsql/raft src/configuration.c; see LICENSE.md.
#include <raftpp/configuration.hpp>

#include <algorithm>

namespace raftpp {
    namespace {

        constexpr uint8_t encoding_format = 1;

        void put_u64(view<byte> output, size_t& offset, uint64_t value) noexcept {
            for (unsigned shift = 0; shift < 64; shift += 8) {
                output[offset++] = static_cast<byte>((value >> shift) & 0xff);
            }
        }

        uint64_t get_u64(view<const byte> input, size_t& offset) noexcept {
            uint64_t value = 0;
            for (unsigned shift = 0; shift < 64; shift += 8) {
                value |= std::to_integer<uint64_t>(input[offset++]) << shift;
            }
            return value;
        }

    }

    error_code configuration::add(server_id id, string_view address, role role) noexcept {
        if (id == 0) {
            return errc::bad_id;
        }
        if (role != role::standby && role != role::voter && role != role::spare) {
            return errc::bad_role;
        }
        if (address.find('\0') != string_view::npos) {
            return errc::invalid;
        }
        for (const auto& member : servers_) {
            if (member.id == id) {
                return errc::duplicate_id;
            }
            if (member.address == address) {
                return errc::duplicate_address;
            }
        }
        try {
            servers_.push_back(server{id, string{address}, role});
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code configuration::remove(server_id id) noexcept {
        const auto found = std::ranges::find(servers_, id, &server::id);
        if (found == servers_.end()) {
            return errc::bad_id;
        }
        servers_.erase(found);
        return {};
    }

    size_t configuration::index_of(server_id id) const noexcept {
        return static_cast<size_t>(std::ranges::find(servers_, id, &server::id) - servers_.begin());
    }

    borrowed<const server> configuration::get(server_id id) const noexcept {
        const auto index = index_of(id);
        return index == size() ? nullptr : &servers_[index];
    }

    size_t configuration::voter_count() const noexcept {
        return static_cast<size_t>(std::ranges::count(servers_, role::voter, &server::role));
    }

    size_t configuration::index_of_voter(server_id id) const noexcept {
        size_t index = 0;
        for (const auto& member : servers_) {
            if (member.id == id) {
                return member.role == role::voter ? index : size();
            }
            if (member.role == role::voter) {
                ++index;
            }
        }
        return size();
    }

    size_t configuration::actual_index_of_voter(size_t index) const noexcept {
        size_t voter_index = 0;
        for (size_t i = 0; i < size(); ++i) {
            if (servers_[i].role == role::voter && voter_index++ == index) {
                return i;
            }
        }
        return size();
    }

    size_t configuration::encoded_size() const noexcept {
        constexpr auto maximum = numeric_limits<size_t>::max();
        size_t size = 9;
        for (const auto& member : servers_) {
            if (size > maximum - 10 || member.address.size() > maximum - size - 10) {
                return 0;
            }
            size += 10 + member.address.size();
        }
        if (size > maximum - 7) {
            return 0;
        }
        return (size + 7) & ~size_t{7};
    }

    error_code configuration::encode(view<byte> output) const noexcept {
        if (empty()) {
            return errc::invalid;
        }
        const auto size = encoded_size();
        if (size == 0) {
            return errc::too_big;
        }
        if (output.size() < size) {
            return errc::too_big;
        }
        size_t offset = 0;
        output[offset++] = static_cast<byte>(encoding_format);
        put_u64(output, offset, servers_.size());
        for (const auto& member : servers_) {
            put_u64(output, offset, member.id);
            for (const auto character : member.address) {
                output[offset++] = static_cast<byte>(static_cast<unsigned char>(character));
            }
            output[offset++] = byte{0};
            output[offset++] = static_cast<byte>(member.role);
        }
        std::fill(output.begin() + static_cast<ptrdiff_t>(offset), output.end(), byte{0});
        return {};
    }

    error_code configuration::encode(buffer& output) const noexcept {
        if (empty()) {
            return errc::invalid;
        }
        const auto size = encoded_size();
        if (size == 0) {
            return errc::too_big;
        }
        try {
            buffer encoded{size};
            if (auto error = encode(encoded.bytes())) {
                return error;
            }
            output.swap(encoded);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code configuration::decode(view<const byte> input) noexcept {
        if (input.size() < 9 || input[0] != static_cast<byte>(encoding_format)) {
            return errc::malformed;
        }
        size_t offset = 1;
        const auto count = get_u64(input, offset);
        // Each server needs an ID, at least a NUL address, and a role byte.
        if (count > (input.size() - offset) / 10) {
            return errc::malformed;
        }
        configuration decoded;
        for (uint64_t i = 0; i < count; ++i) {
            if (input.size() - offset < 10) {
                return errc::malformed;
            }
            const auto id = get_u64(input, offset);
            const auto remaining = input.subspan(offset);
            const auto end = std::ranges::find(remaining, byte{0});
            const auto length = static_cast<size_t>(end - remaining.begin());
            if (end == remaining.end() || remaining.size() - length < 2) {
                return errc::malformed;
            }
            const string_view address{reinterpret_cast<const char*>(remaining.data()), length};
            offset += length + 1;
            const auto role = static_cast<raftpp::role>(std::to_integer<uint8_t>(input[offset++]));
            if (auto error = decoded.add(id, address, role)) {
                return error == errc::out_of_memory ? error : make_error_code(errc::malformed);
            }
        }
        servers_.swap(decoded.servers_);
        return {};
    }

}
