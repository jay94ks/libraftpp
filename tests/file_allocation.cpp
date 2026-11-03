#include "disk.hpp"
#include "file_allocation.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace r = raftpp;
namespace d = raftpp::detail;
namespace {
    void check(bool value, r::string_view message) {
        if (!value)
            throw r::system_error(r::make_error_code(r::errc::io_error), r::string{message});
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-file-allocation-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "allocation directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    r::size_t descriptors() {
        return static_cast<r::size_t>(
            std::distance(r::fs::directory_iterator{"/proc/self/fd"}, r::fs::directory_iterator{}));
    }
    r::buffer read(const r::fs::path& path) {
        r::buffer value{static_cast<r::size_t>(r::fs::file_size(path))};
        std::ifstream input{path, std::ios::binary};
        input.read(reinterpret_cast<char*>(value.data()),
                   static_cast<std::streamsize>(value.size()));
        check(bool(input), "read reserved file");
        return value;
    }
}
int main() {
    try {
        const auto initial_descriptors = descriptors();
        unsigned cases = 0, fallback = 0, failures = 0;
        auto compare = [&](r::size_t size, raftpp_allocation_plan plan, r::error_code expected,
                           int cause = 0) {
            directory cpp;
            d::disk_storage disk{cpp.path};
            check(!disk.open(), "open allocation store");
            raftpp_allocation_trace actual{};
            const auto sentinel = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            check(sentinel >= 0, "open output sentinel");
            d::file_handle output{sentinel};
            r::error_info info = r::make_error_info(r::errc::busy, "old");
            raftpp_allocation_begin(&plan);
            const auto status = disk.prepare_segment(1, size, output, &info);
            raftpp_allocation_end(&actual);
            check(status == expected && actual.allocations == 1 && actual.writes <= 64,
                  "allocation outcome and native-call bound");
            if (status) {
                ++failures;
                check(output.get() == sentinel && fcntl(sentinel, F_GETFD) != -1,
                      "failed preparation preserves the caller's owning output handle");
                check(info.code == status &&
                          info.cause == r::error_code{cause, std::generic_category()} &&
                          info.text().find("open-1") != r::string_view::npos,
                      "failure diagnostic retains the actual native error and segment path");
                check(r::fs::is_empty(cpp.path), "preparation removes partially allocated files");
            } else {
                check(!info.code && output.get() != sentinel,
                      "success replaces output handle and clears stale diagnosis");
                const auto b = read(cpp.path / "open-1");
                check(b.size() == size &&
                          std::ranges::all_of(b.bytes(),
                                              [](r::byte value) { return value == r::byte{}; }),
                      "reservation creates the prescribed zero-filled file size and bytes");
                if (plan.allocation_error == EOPNOTSUPP) {
                    ++fallback;
                    const auto increment =
                        plan.block_size == 0
                            ? 512U
                            : static_cast<unsigned>(std::min(4096L, plan.block_size));
                    check(actual.queries == 1 &&
                              actual.writes == (size + increment - 1) / increment &&
                              actual.positions[actual.writes - 1] == size - 1,
                          "fallback touches one byte per block and the final file byte");
                } else
                    check(actual.queries == 0 && actual.writes == 0,
                          "native allocation needs no emulation");
            }
            check(!output.close() && descriptors() == initial_descriptors,
                  "allocation success and failure leave no descriptor leak");
            ++cases;
        };
        for (const long block : {0, 512, 1024, 2048, 4096, 8192})
            for (const r::size_t size : {1, 511, 512, 513, 4095, 4096, 4097, 8192, 8193})
                compare(size, {block, EOPNOTSUPP, 0, -1, 0}, {});
        compare(4096, {-1, 0, 0, -1, 0}, {});
        for (const int error : {EIO, ENOMEM, EINVAL, EACCES})
            compare(4096, {-1, error, 0, -1, 0}, r::errc::io_error, error);
        for (const int error : {EIO, ENOMEM, EPERM})
            compare(8193, {512, EOPNOTSUPP, error, -1, 0}, r::errc::io_error, error);
        for (const int error : {EIO, ENOSPC, EINVAL, EINTR})
            for (const int after : {0, 1, 3})
                compare(8193, {512, EOPNOTSUPP, 0, after, error}, r::errc::io_error, error);
        check(cases == 74 && fallback == 54 && failures == 19,
              "all native and fallback cases executed");
        std::cout << "file allocation: " << cases << " cases, " << fallback
                  << " fallback successes, " << failures << " failures\n";
        return 0;
    } catch (const std::exception& error) {
        raftpp_allocation_trace ignored{};
        raftpp_allocation_end(&ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
