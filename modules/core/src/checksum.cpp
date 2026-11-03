// C++ port of cowsql/raft src/byte.c and raft_digest; see LICENSE.md.
// SHA-1 in the reference is derived from Steve Reid's public-domain sha1.c.
#include <raftpp/checksum.hpp>
#include "byte.hpp"

#include <algorithm>
#include <bit>

namespace raftpp {

    namespace {
        constexpr auto crc_table = [] {
            array<uint32_t, 256> values{};
            for (uint32_t i = 0; i < values.size(); ++i) {
                uint32_t value = i << 24;
                for (unsigned bit = 0; bit < 8; ++bit)
                    value = (value << 1) ^ ((value & 0x80000000U) ? 0x04c11db7U : 0);
                values[i] = value;
            }
            return values;
        }();
    }

    uint32_t crc32(view<const byte> data, uint32_t initial) noexcept {
        for (auto value : data)
            initial = (initial << 8) ^
                      crc_table[((initial >> 24) ^ std::to_integer<uint8_t>(value)) & 255];
        return initial;
    }

    void sha1::reset() noexcept {
        *this = sha1{};
    }

    void sha1::transform(view<const byte, 64> block) noexcept {
        array<uint32_t, 80> words{};
        for (size_t i = 0; i < 16; ++i)
            for (size_t j = 0; j < 4; ++j)
                words[i] = (words[i] << 8) | std::to_integer<uint8_t>(block[4 * i + j]);
        for (size_t i = 16; i < 80; ++i)
            words[i] = std::rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        auto [a, b, c, d, e] = state_;
        for (size_t i = 0; i < 80; ++i) {
            const auto f = i < 20   ? ((b & c) | (~b & d))
                           : i < 40 ? (b ^ c ^ d)
                           : i < 60 ? ((b & c) | (b & d) | (c & d))
                                    : (b ^ c ^ d);
            const uint32_t k = i < 20   ? 0x5a827999U
                               : i < 40 ? 0x6ed9eba1U
                               : i < 60 ? 0x8f1bbcdcU
                                        : 0xca62c1d6U;
            const uint32_t next = std::rotl(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = next;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    void sha1::update(view<const byte> data) noexcept {
        auto offset = static_cast<size_t>(size_ % 64);
        size_ += static_cast<uint64_t>(data.size());
        if (offset != 0) {
            const auto count = std::min(data.size(), 64 - offset);
            if (count != 0)
                std::copy_n(data.begin(), count, buffer_.begin() + static_cast<ptrdiff_t>(offset));
            data = data.subspan(count);
            offset += count;
            if (offset != 64)
                return;
            transform(buffer_);
        }
        while (data.size() >= 64) {
            transform(data.first<64>());
            data = data.subspan(64);
        }
        if (!data.empty())
            std::copy(data.begin(), data.end(), buffer_.begin());
    }

    array<byte, 20> sha1::digest() const noexcept {
        auto state = *this;
        const auto bits = size_ * 8;
        array<byte, 128> suffix{};
        suffix[0] = byte{0x80};
        const auto padding =
            static_cast<size_t>(size_ % 64 < 56 ? 56 - size_ % 64 : 120 - size_ % 64);
        for (size_t i = 0; i < 8; ++i)
            suffix[padding + i] = static_cast<byte>(bits >> (56 - 8 * i));
        state.update(view<const byte>{suffix}.first(padding + 8));
        array<byte, 20> result{};
        for (size_t i = 0; i < result.size(); ++i)
            result[i] = static_cast<byte>(state.state_[i / 4] >> (24 - 8 * (i % 4)));
        return result;
    }

    uint64_t digest(string_view text, uint64_t n) noexcept {
        sha1 state;
        state.update({reinterpret_cast<const byte*>(text.data()), text.size()});
        array<byte, 8> number{};
        detail::store_little_endian<uint64_t>(n, number);
        state.update(number);
        const auto value = state.digest();
        return detail::load_little_endian<uint64_t>(view<const byte>{value}.last<8>());
    }

}
