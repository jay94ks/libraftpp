#ifndef RAFTPP_EXAMPLE_RUNTIME_HPP
#define RAFTPP_EXAMPLE_RUNTIME_HPP

#include <raftpp/common.hpp>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <uv.h>

namespace raftpp::example {
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
    template <typename T> void close_handle(T& value, bool initialized) noexcept {
        auto handle = reinterpret_cast<uv_handle_t*>(&value);
        if (initialized && !uv_is_closing(handle))
            uv_close(handle, nullptr);
    }
    inline unsigned number(string_view text, unsigned maximum = numeric_limits<unsigned>::max(),
                           unsigned minimum = 0) {
        unsigned value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            value < minimum || value > maximum)
            throw invalid_argument("invalid numeric argument: " + string{text});
        return value;
    }
    inline void ensure_directory(const fs::path& path) {
        if (fs::create_directories(path))
            fs::permissions(path, fs::perms::owner_all);
        if (!fs::is_directory(path))
            throw invalid_argument("not a directory: " + path.native());
    }
    struct server_settings {
        unsigned base_port = 9000;
        unsigned apply_interval = 125;
        unsigned snapshot_threshold = 64;
        unsigned snapshot_trailing = 16;
    };
    inline bool setting(server_settings& settings, string_view name, string_view value) {
        if (name == "--base-port")
            settings.base_port = number(value, 65532, 1);
        else if (name == "--apply-interval")
            settings.apply_interval = number(value, numeric_limits<unsigned>::max(), 1);
        else if (name == "--snapshot-threshold")
            settings.snapshot_threshold = number(value);
        else if (name == "--snapshot-trailing")
            settings.snapshot_trailing = number(value);
        else
            return false;
        return true;
    }
    inline string address(const server_settings& settings, unsigned id) {
        return "127.0.0.1:" + std::to_string(settings.base_port + id);
    }
}
#endif // RAFTPP_EXAMPLE_RUNTIME_HPP
