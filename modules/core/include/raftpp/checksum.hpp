#ifndef RAFTPP_CHECKSUM_HPP
#define RAFTPP_CHECKSUM_HPP

#include <raftpp/common.hpp>
#include <raftpp/export.hpp>

namespace raftpp {

    // Raft's non-reflected CRC32, polynomial 0x04c11db7, no final XOR.
    [[nodiscard]] RAFTPP_API uint32_t crc32(view<const byte> data, uint32_t initial = 0) noexcept;

    class RAFTPP_API sha1 {
    public:
        sha1() noexcept = default;
        void reset() noexcept;
        void update(view<const byte> data) noexcept;
        // Does not consume the state: callers can continue updating afterward.
        [[nodiscard]] array<byte, 20> digest() const noexcept;

    private:
        void transform(view<const byte, 64> block) noexcept;
        array<uint32_t, 5> state_{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
        array<byte, 64> buffer_{};
        uint64_t size_ = 0;
    };

    // Last eight SHA-1 bytes interpreted little endian after hashing text and n LE.
    [[nodiscard]] RAFTPP_API uint64_t digest(string_view text, uint64_t n) noexcept;

}

#endif // RAFTPP_CHECKSUM_HPP
