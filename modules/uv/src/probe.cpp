// C++ port of cowsql/raft filesystem capability probes; see LICENSE.md.
#include "probe.hpp"
#include "aio.hpp"
#include <algorithm>
#include <fcntl.h>
#include <sys/vfs.h>

namespace raftpp::detail {
    error_code probe_filesystem(const fs::path& directory, disk_capabilities& output,
                                writer_hook hook, error_info* diagnostic) noexcept {
        if (diagnostic)
            *diagnostic = {};
        auto fail = [&](string_view operation, int64_t native = 0,
                        errc code = errc::io_error) -> error_code {
            const auto cause =
                native < 0 && native >= -static_cast<int64_t>(numeric_limits<int>::max())
                    ? error_code{static_cast<int>(-native), std::generic_category()}
                    : error_code{};
            if (diagnostic)
                *diagnostic = make_error_info(code, operation, directory.native(), cause);
            return code;
        };
        try {
            const auto path = directory / ".probe";
            (void)unlink(path.c_str());
            file_handle file;
            if (auto error =
                    file_handle::open(path, O_WRONLY | O_CREAT | O_EXCL, 0600, file, diagnostic))
                return error;
            (void)unlink(path.c_str());
            if (auto error = file.allocate(4096, diagnostic))
                return error;
            auto point = [&](writer_operation operation, int64_t value = 0) {
                return hook ? hook(operation, value) : value;
            };
            disk_capabilities result;
            auto status = point(writer_operation::direct);
            if (status == 0) {
                const auto flags = fcntl(file.get(), F_GETFL);
                status = flags < 0 || fcntl(file.get(), F_SETFL, flags | O_DIRECT) < 0 ? -errno : 0;
            }
            if (status != 0) {
                if (status != -EINVAL)
                    return fail("probe.direct", status);
                struct statfs info {};
                if (fstatfs(file.get(), &info) != 0)
                    return fail("probe.statfs", -errno);
                if (info.f_type != 0x01021994 && info.f_type != 0x2fc12fc1 &&
                    info.f_type != 0x24051905)
                    return fail("probe.direct.filesystem", status);
                output = result;
                return {};
            }
            size_t block = 4096;
            for (; block >= 512; block /= 2) {
                aligned_buffer data{block, block};
                std::ranges::fill(data.bytes(), byte{});
                const auto written = ::write(file.get(), data.data(), data.size());
                const auto error = errno;
                if (written == static_cast<ssize_t>(block))
                    break;
                if (written >= 0)
                    return fail("probe.short_write");
                if (error == EINVAL && block == 4096) {
                    block = 0;
                    break;
                }
                if (error != EIO && error != EOPNOTSUPP)
                    return fail("probe.write", -error);
            }
            if (block < 512) {
                output = result;
                return {};
            }
            result.block_size = block;
            result.direct = true;
            // Context is destroyed before its buffer, even on syscall errors.
            aligned_buffer data{block, block};
            std::ranges::fill(data.bytes(), byte{});
            struct context_owner {
                aio_context_t value = 0;
                int close() noexcept {
                    if (value == 0)
                        return 0;
                    const auto status = native_aio::destroy(value);
                    if (status == 0)
                        value = 0;
                    return status;
                }
                ~context_owner() {
                    if (value != 0)
                        (void)native_aio::destroy(value);
                }
            } context;
            status = point(writer_operation::setup);
            if (status == 0)
                status = native_aio::setup(1, context.value);
            if (status != 0)
                return fail("probe.io_setup", status);
            auto finish = [&]() -> error_code {
                const auto status = point(writer_operation::destroy_result, context.close());
                return status == 0 ? error_code{} : fail("probe.io_destroy", status);
            };
            iocb request{};
            request.aio_lio_opcode = IOCB_CMD_PWRITE;
            request.aio_buf = reinterpret_cast<uintptr_t>(data.data());
            request.aio_nbytes = data.size();
            request.aio_fildes = static_cast<uint32_t>(file.get());
            request.aio_rw_flags = RWF_NOWAIT | RWF_DSYNC;
            status = point(writer_operation::submit_nowait);
            if (status == 0)
                status = native_aio::submit(context.value, request);
            if (status == -EOPNOTSUPP || status == -EINVAL) {
                if (auto error = finish())
                    return error;
                output = result;
                return {};
            }
            if (status != 0)
                return fail("probe.io_submit", status);
            io_event event{};
            const auto events = native_aio::get_events(context.value, 1, 1, &event, nullptr);
            if (events != 1)
                return fail("probe.io_getevents", events);
            const auto completed = point(writer_operation::poll_result, event.res);
            if (auto error = finish())
                return error;
            result.async = completed == static_cast<int64_t>(block) || completed == -EAGAIN;
            output = result;
            return {};
        } catch (const bad_alloc&) {
            return fail("probe", 0, errc::out_of_memory);
        } catch (const length_error&) {
            return fail("probe", 0, errc::too_big);
        } catch (...) {
            return fail("probe");
        }
    }
}
