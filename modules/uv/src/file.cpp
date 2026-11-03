// File-allocation fallback ported from cowsql/raft uv_os.c; see LICENSE.md.
#include "file.hpp"
#include <fcntl.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <sys/vfs.h>
#include <unistd.h>
#include <uv.h>

namespace raftpp::detail {

    namespace {
        error_code complete(error_info* diagnostic, error_code status, string_view operation,
                            string_view resource, int cause = 0) noexcept {
            if (diagnostic)
                *diagnostic = make_error_info(status, operation, resource,
                                              cause ? error_code{cause, std::generic_category()}
                                                    : error_code{});
            return status;
        }
    }

    file_handle::~file_handle() {
        (void)close();
    }
    file_handle& file_handle::operator=(file_handle&& other) noexcept {
        if (this != &other) {
            (void)close();
            descriptor_ = other.release();
        }
        return *this;
    }
    error_code file_handle::close(error_info* diagnostic) noexcept {
        if (descriptor_ < 0)
            return complete(diagnostic, {}, {}, {});
        array<char, 32> resource{};
        std::snprintf(resource.data(), resource.size(), "fd %d", descriptor_);
        uv_fs_t request{};
        const auto status = uv_fs_close(nullptr, &request, release(), nullptr);
        uv_fs_req_cleanup(&request);
        return complete(diagnostic, status < 0 ? make_error_code(errc::io_error) : error_code{},
                        "file.close", resource.data(), status < 0 ? -status : 0);
    }
    error_code file_handle::open(const fs::path& path, int flags, int mode, file_handle& output,
                                 error_info* diagnostic) noexcept {
        uv_fs_t request{};
        const auto descriptor =
            uv_fs_open(nullptr, &request, path.c_str(), flags | O_CLOEXEC, mode, nullptr);
        uv_fs_req_cleanup(&request);
        if (descriptor < 0)
            return complete(diagnostic,
                            descriptor == UV_ENOMEM ? make_error_code(errc::out_of_memory)
                                                    : make_error_code(errc::io_error),
                            "file.open", path.native(), -descriptor);
        output = file_handle{descriptor};
        return complete(diagnostic, {}, {}, {});
    }
    error_code file_handle::allocate(size_t size, error_info* diagnostic) noexcept {
        array<char, 32> resource{};
        std::snprintf(resource.data(), resource.size(), "fd %d", descriptor_);
        if (descriptor_ < 0 || size == 0)
            return complete(diagnostic, errc::invalid, "file.allocate", resource.data());
        if (size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
            return complete(diagnostic, errc::too_big, "file.allocate", resource.data());
        auto status = posix_fallocate(descriptor_, 0, static_cast<off_t>(size));
        if (status == EOPNOTSUPP) {
            struct statfs info {};
            if (fstatfs(descriptor_, &info) != 0)
                return complete(diagnostic, errc::io_error, "file.statfs", resource.data(), errno);
            const auto increment = info.f_bsize <= 0 ? 512 : std::min<long>(4096, info.f_bsize);
            for (uint64_t offset = (size - 1) % static_cast<uint64_t>(increment); offset < size;
                 offset += static_cast<uint64_t>(increment))
                if (const auto written = pwrite(descriptor_, "", 1, static_cast<off_t>(offset));
                    written != 1)
                    return complete(diagnostic, errc::io_error, "file.allocate.write",
                                    resource.data(), written < 0 ? errno : 0);
            status = 0;
        }
        return complete(diagnostic,
                        status == 0        ? error_code{}
                        : status == ENOSPC ? make_error_code(errc::no_space)
                                           : make_error_code(errc::io_error),
                        "file.allocate", resource.data(), status);
    }

}
