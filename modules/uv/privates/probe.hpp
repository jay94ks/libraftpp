#ifndef RAFTPP_DETAIL_PROBE_HPP
#define RAFTPP_DETAIL_PROBE_HPP

#include <raftpp/uv/export.hpp>
#include "writer.hpp"

#if RAFTPP_WITH_UV
namespace raftpp::detail {
    struct disk_capabilities {
        size_t block_size = 4096;
        bool direct = false, async = false;
    };
    // Synchronous initialization probe. Output is preserved on failure.
    [[nodiscard]] RAFTPP_UV_API error_code
    probe_filesystem(const fs::path& directory, disk_capabilities& output, writer_hook hook = {},
                     error_info* diagnostic = nullptr) noexcept;
}
#endif
#endif // RAFTPP_DETAIL_PROBE_HPP
