#ifndef RAFTPP_PRIVATE_SNAPSHOT_FORMAT_HPP
#define RAFTPP_PRIVATE_SNAPSHOT_FORMAT_HPP

#include <raftpp/buffer.hpp>
#include <algorithm>

namespace raftpp::detail {
    inline constexpr array<byte, 8> snapshot_envelope_magic{
        byte{'R'}, byte{'A'}, byte{'F'}, byte{'T'}, byte{'P'}, byte{'P'}, byte{1}, byte{0}};
    inline bool has_snapshot_envelope(view<const byte> input) noexcept {
        return input.size() >= snapshot_envelope_magic.size() &&
               std::ranges::equal(input.first(snapshot_envelope_magic.size()),
                                  snapshot_envelope_magic);
    }
}
#endif
