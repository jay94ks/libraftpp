#ifndef RAFTPP_BENCHMARK_DISK_HPP
#define RAFTPP_BENCHMARK_DISK_HPP

#include "disk_options.hpp"
#include "device.hpp"
#include "runtime.hpp"
#include "report.hpp"
#include "file.hpp"
#include <csignal>

namespace raftpp::benchmark {
    class disk_target {
    public:
        explicit disk_target(const disk_options& settings);
        disk_target(const disk_target&) = delete;
        disk_target& operator=(const disk_target&) = delete;
        void prepare(borrowed<const volatile std::sig_atomic_t> stop = nullptr);
        void cleanup();
        int descriptor() const noexcept {
            return file_.get();
        }
        const device_info& info() const noexcept {
            return info_;
        }
        const fs::path& path() const noexcept {
            return path_;
        }

    private:
        disk_options settings_;
        optional<temporary_directory> workspace_;
        detail::file_handle file_;
        fs::path path_;
        device_info info_;
    };
    report disk(const disk_options& settings);
}
#endif // RAFTPP_BENCHMARK_DISK_HPP
