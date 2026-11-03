// LZ4 frame handling follows cowsql/raft src/compress.c; see LICENSE.md.
#include <raftpp/uv/compression.hpp>

namespace raftpp {

    bool is_compressed(view<const byte> input) noexcept {
        return input.size() >= 4 && input[0] == byte{0x04} && input[1] == byte{0x22} &&
               input[2] == byte{0x4d} && input[3] == byte{0x18};
    }

#if !RAFTPP_WITH_LZ4
    error_code decompress(view<const byte>, buffer&, size_t) noexcept {
        return errc::invalid;
    }
#endif

}
