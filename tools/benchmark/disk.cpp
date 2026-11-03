// C++ port of cowsql/raft tools/benchmark/disk.c and fs.c; see LICENSE.md.
#include "disk.hpp"
#include "uring.hpp"
#include "profiler.hpp"
#include <raftpp/buffer.hpp>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace raftpp::benchmark {
    namespace {
        volatile std::sig_atomic_t interrupted = 0;
        void interrupt(int) noexcept {
            interrupted = 1;
        }
        struct signal_guard {
            using handler = void (*)(int);
            handler previous_int = SIG_DFL, previous_term = SIG_DFL;
            signal_guard() {
                interrupted = 0;
                previous_int = std::signal(SIGINT, interrupt);
                if (previous_int == SIG_ERR)
                    throw system_error(errno, std::generic_category(),
                                       "install disk SIGINT handler");
                previous_term = std::signal(SIGTERM, interrupt);
                if (previous_term == SIG_ERR) {
                    const auto cause = errno;
                    std::signal(SIGINT, previous_int);
                    throw system_error(cause, std::generic_category(),
                                       "install disk SIGTERM handler");
                }
            }
            ~signal_guard() {
                std::signal(SIGINT, previous_int);
                std::signal(SIGTERM, previous_term);
            }
        };
        void check_stop(borrowed<const volatile std::sig_atomic_t> stop) {
            if (stop && *stop)
                throw system_error(std::make_error_code(std::errc::interrupted),
                                   "disk benchmark interrupted");
        }
        void checked(int result, string_view operation) {
            if (result < 0)
                throw system_error(errno, std::generic_category(), string{operation});
        }
    }
    disk_target::disk_target(const disk_options& settings) : settings_(settings) {
        validate_disk(settings_);
        struct stat status {};
        checked(stat(settings_.directory.c_str(), &status), "stat disk benchmark destination");
        int flags = O_RDWR | O_CLOEXEC | O_DIRECT;
        if (S_ISBLK(status.st_mode))
            path_ = settings_.directory;
        else if (S_ISDIR(status.st_mode)) {
            workspace_.emplace(settings_.directory);
            path_ = workspace_->path() / "data";
            flags |= O_CREAT | O_EXCL;
        } else
            throw invalid_argument("disk --dir must be an existing directory or block device");
        const auto error = detail::file_handle::open(path_, flags, 0600, file_);
        if (error)
            throw system_error(error, "open disk benchmark " + path_.string());
        struct stat opened {};
        checked(fstat(file_.get(), &opened), "fstat disk benchmark destination");
        if (workspace_ ? !S_ISREG(opened.st_mode) : !S_ISBLK(opened.st_mode))
            throw std::runtime_error("disk destination type changed during open");
        info_ = inspect_device(path_);
        if (info_.raw) {
            uint64_t capacity = 0;
            checked(ioctl(file_.get(), BLKGETSIZE64, &capacity), "read block-device capacity");
            if (settings_.size > capacity)
                throw invalid_argument("disk size exceeds block-device capacity");
        }
    }
    void disk_target::prepare(borrowed<const volatile std::sig_atomic_t> stop) {
        check_stop(stop);
        if (info_.raw)
            return; // Never preallocate/truncate a block device.
        const auto allocated = posix_fallocate(file_.get(), 0, static_cast<off_t>(settings_.size));
        if (allocated)
            throw system_error(allocated, std::generic_category(), "allocate disk benchmark file");
        aligned_buffer zeros{settings_.buffer, std::max<size_t>(4096, settings_.buffer)};
        std::fill(zeros.bytes().begin(), zeros.bytes().end(), byte{0});
        for (uint64_t offset = 0; offset < settings_.size; offset += zeros.size()) {
            check_stop(stop);
            ssize_t count;
            do {
                count = pwrite(file_.get(), zeros.data(), zeros.size(), static_cast<off_t>(offset));
            } while (count < 0 && errno == EINTR && !(stop && *stop));
            if (count < 0)
                checked(-1, "prefill disk benchmark file");
            if (static_cast<size_t>(count) != zeros.size())
                throw system_error(std::make_error_code(std::errc::io_error), "short disk prefill");
        }
        checked(fsync(file_.get()), "sync disk benchmark file");
        detail::file_handle directory;
        const auto error = detail::file_handle::open(
            workspace_->path(), O_RDONLY | O_CLOEXEC | O_DIRECTORY, 0, directory);
        if (error)
            throw system_error(error, "open disk benchmark directory");
        checked(fsync(directory.get()), "sync disk benchmark directory");
    }
    void disk_target::cleanup() {
        if (const auto error = file_.close())
            throw system_error(error, "close disk benchmark destination");
        if (workspace_)
            workspace_->cleanup();
    }
    report disk(const disk_options& settings) {
        validate_disk(settings);
#if RAFTPP_BENCHMARK_HAS_IO_URING
        signal_guard signals;
        disk_target destination{settings};
        destination.prepare(&interrupted);
        optional<kernel_profiler> profiler;
        if (settings.perf || !settings.traces.empty())
            profiler.emplace(destination.info(), settings.perf, settings.traces, settings.tracefs);
        latency_samples samples{static_cast<size_t>(settings.operations())};
        const auto begin = clock::now();
        const auto deadline = begin + chrono::milliseconds{settings.timeout};
        {
            uring_writer writer{destination.descriptor(), settings.buffer};
            writer.write(0, deadline, &interrupted); // Warm-up outside individual latency samples.
            if (profiler)
                profiler->start();
            for (uint64_t offset = 0; offset < settings.size; offset += settings.buffer) {
                const auto start = clock::now();
                writer.write(offset, deadline, &interrupted);
                samples.add(elapsed(start));
                if (profiler)
                    profiler->poll();
            }
            if (profiler)
                profiler->stop();
            if (const auto error = writer.close())
                throw system_error(error, "close disk io_uring");
        }
        const auto duration =
            elapsed(begin); // Includes ring setup, warm-up and ring teardown, as in C.
        destination.cleanup();
        report result;
        const auto name = "disk:" + std::to_string(settings.buffer);
        if (settings.perf)
            profiler->report_into(result, settings.buffer, settings.operations(),
                                  static_cast<unsigned>(getpid()));
        else {
            result.add(name, metric_kind::latency, samples.summarize());
            result.add(
                name, metric_kind::throughput,
                throughput(static_cast<double>(settings.size) / (1024.0 * 1024.0), duration));
        }
        return result;
#else
        throw system_error(std::make_error_code(std::errc::operation_not_supported),
                           "io_uring unavailable at build time");
#endif
    }
}
