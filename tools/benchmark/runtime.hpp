#ifndef RAFTPP_BENCHMARK_RUNTIME_HPP
#define RAFTPP_BENCHMARK_RUNTIME_HPP

#include <raftpp/common.hpp>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <uv.h>
#include <unistd.h>

namespace raftpp::benchmark {
    inline void check_uv(int status, string_view operation) {
        if (status)
            throw std::runtime_error(string{operation} + ": " + uv_strerror(status));
    }
    struct event_loop {
        uv_loop_t value{};
        event_loop() {
            check_uv(uv_loop_init(&value), "uv_loop_init");
        }
        ~event_loop() {
            if (uv_loop_close(&value))
                std::abort();
        }
        event_loop(const event_loop&) = delete;
        event_loop& operator=(const event_loop&) = delete;
    };
    template <typename T> void close_handle(T& value, bool live) noexcept {
        auto handle = reinterpret_cast<uv_handle_t*>(&value);
        if (live && !uv_is_closing(handle))
            uv_close(handle, nullptr);
    }
    class temporary_directory {
    public:
        explicit temporary_directory(const fs::path& parent) {
            if (!fs::is_directory(parent))
                throw invalid_argument("benchmark --dir must be an existing directory");
            string pattern = (parent / "bench-XXXXXX").native();
            if (!mkdtemp(pattern.data()))
                throw system_error(errno, std::generic_category(), "create benchmark directory");
            // Move allocation before ownership is committed; pattern's storage
            // already owns the exact path if constructing filesystem::path fails.
            try {
                path_ = pattern;
            } catch (...) {
                (void)rmdir(pattern.c_str());
                throw;
            }
            owned_ = true;
        }
        ~temporary_directory() {
            if (owned_) {
                error_code ignored;
                fs::remove_all(path_, ignored);
            }
        }
        temporary_directory(const temporary_directory&) = delete;
        temporary_directory& operator=(const temporary_directory&) = delete;
        const fs::path& path() const noexcept {
            return path_;
        }
        void cleanup() {
            if (!owned_)
                return;
            fs::remove_all(path_);
            owned_ = false;
        }

    private:
        fs::path path_;
        bool owned_ = false;
    };
}
#endif // RAFTPP_BENCHMARK_RUNTIME_HPP
