#include "probe.hpp"

#include <cerrno>
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
            r::string name = "/tmp/raftpp-probe-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "probe directory");
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
}
int main() {
    try {
        directory cpp;
        const auto before = descriptors();
        unsigned contexts = 0;
        d::disk_capabilities baseline;
        check(!d::probe_filesystem(cpp.path, baseline,
                                   [&](auto operation, auto value) {
                                       if (operation == d::writer_operation::destroy_result)
                                           ++contexts;
                                       return value;
                                   }),
              "native filesystem probe");
        if (!baseline.direct || !contexts) {
            std::cout << "native direct-AIO completion probe unavailable\n";
            return 77;
        }
        for (const int error : {0, EIO, ENOMEM, EINVAL}) {
            const auto expected = error == 0 ? 0 : r::make_error_code(r::errc::io_error).value();
            unsigned destructions = 0;
            d::disk_capabilities output{999, true, true};
            r::error_info info;
            const auto actual = d::probe_filesystem(
                cpp.path, output,
                [&](auto operation, auto value) {
                    if (operation == d::writer_operation::destroy_result) {
                        check(value == 0, "actual C++ context cleanup completed");
                        ++destructions;
                        return error == 0 ? value : -static_cast<r::int64_t>(error);
                    }
                    return value;
                },
                &info);
            check(destructions == 1 && actual.value() == expected,
                  "C++ cleanup-error result matches expected result");
            if (error == 0) {
                check(output.direct && output.block_size == baseline.block_size &&
                          output.async == baseline.async && !info.code,
                      "normal probe capabilities preserve geometry without stale diagnostic");
            } else {
                check(output.block_size == 999 && output.direct && output.async,
                      "cleanup failure preserves all output fields");
                check(info.code == actual &&
                          info.cause == r::error_code{error, std::generic_category()} &&
                          info.text().find("probe.io_destroy") != r::string_view::npos &&
                          info.text().find(cpp.path.native()) != r::string_view::npos,
                      "cleanup failure preserves operation/path/native cause");
            }
            check(descriptors() == before && r::fs::is_empty(cpp.path),
                  "probes release descriptors/files on every path");
            std::cout << "destroy errno=" << error << " expected=" << expected
                      << " actual=" << actual.value() << '\n';
        }
        // Unsupported submission is a successful capability result only after
        // cleanup succeeds. A primary failure still retains its own diagnosis.
        for (const int submission : {-EOPNOTSUPP, -EINVAL, -EACCES}) {
            for (bool fail_cleanup : {false, true}) {
                d::disk_capabilities output{999, true, true};
                r::error_info info;
                unsigned destructions = 0;
                const auto status = d::probe_filesystem(
                    cpp.path, output,
                    [&](auto operation, auto value) {
                        if (operation == d::writer_operation::submit_nowait)
                            return static_cast<r::int64_t>(submission);
                        if (operation == d::writer_operation::destroy_result) {
                            check(value == 0, "fallback context cleanup completed");
                            ++destructions;
                            return fail_cleanup ? r::int64_t{-EIO} : value;
                        }
                        return value;
                    },
                    &info);
                if (submission == -EACCES) {
                    check(status == r::errc::io_error && destructions == 0 &&
                              info.cause == std::errc::permission_denied &&
                              info.text().find("probe.io_submit") != r::string_view::npos &&
                              output.block_size == 999,
                          "best-effort cleanup preserves the primary submission error");
                } else if (fail_cleanup) {
                    check(status == r::errc::io_error && destructions == 1 &&
                              info.cause == std::errc::io_error && output.block_size == 999,
                          "unsupported probe cannot hide cleanup failure");
                } else {
                    check(!status && destructions == 1 && output.direct && !output.async &&
                              !info.code,
                          "unsupported submission publishes synchronous capability after cleanup");
                }
                check(descriptors() == before && r::fs::is_empty(cpp.path),
                      "fallback/primary-error cleanup");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
