#ifndef RAFTPP_BENCHMARK_DISK_OPTIONS_HPP
#define RAFTPP_BENCHMARK_DISK_OPTIONS_HPP

// C++ port of cowsql/raft tools/benchmark/disk_options/parse; see LICENSE.md.
#include "options.hpp"
#include <bit>
#include <algorithm>

namespace raftpp::benchmark {
    struct disk_options {
        fs::path directory = ".";
        size_t buffer = 4096;
        uint64_t size = 8 * 1024 * 1024;
        unsigned timeout = 30000;
        bool perf = false;
        vector<string> traces;
        fs::path tracefs = "/sys/kernel/tracing";
        bool help = false;
        uint64_t operations() const noexcept {
            return size / buffer;
        }
    };
    inline void validate_disk(const disk_options& value) {
        if (value.buffer < 512 || value.buffer > 1024 * 1024 ||
            !std::has_single_bit(value.buffer) || value.size == 0 ||
            value.size > numeric_limits<uint32_t>::max() || value.size % value.buffer ||
            value.timeout == 0 || value.directory.empty())
            throw invalid_argument(
                "disk requires a power-of-two buffer of 512..1048576 bytes and a positive total at most UINT32_MAX divisible by the buffer");
        for (size_t i = 0; i < value.traces.size(); ++i) {
            const auto& name = value.traces[i];
            if (name.empty() ||
                name.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                    string::npos ||
                std::find(value.traces.begin(), value.traces.begin() + static_cast<ptrdiff_t>(i),
                          name) != value.traces.begin() + static_cast<ptrdiff_t>(i))
                throw invalid_argument("invalid or duplicate trace subsystem");
        }
        if ((value.perf || !value.traces.empty()) && value.tracefs.empty())
            throw invalid_argument("empty tracefs path");
    }
    inline disk_options parse_disk(view<const string_view> arguments) {
        disk_options result;
        for (size_t i = 0; i < arguments.size(); ++i) {
            auto key = arguments[i];
            if (key == "--help" || key == "-?") {
                result.help = true;
                continue;
            }
            if (key == "--perf" || key == "-p") {
                result.perf = true;
                continue;
            }
            string_view value;
            if (key.size() > 2 && key.front() == '-' && key[1] != '-') {
                value = key.substr(2);
                key = key.substr(0, 2);
            } else if (const auto equal = key.find('='); equal != string_view::npos) {
                value = key.substr(equal + 1);
                key = key.substr(0, equal);
            } else {
                if (++i == arguments.size())
                    throw invalid_argument("missing disk option value");
                value = arguments[i];
            }
            if (key == "--dir" || key == "-d")
                result.directory = value;
            else if (key == "--buf" || key == "-b") {
                const auto number = bytes(value);
                if (number > numeric_limits<size_t>::max())
                    throw invalid_argument("disk buffer size overflow");
                result.buffer = static_cast<size_t>(number);
            } else if (key == "--size" || key == "-s")
                result.size = bytes(value);
            else if (key == "--tracefs")
                result.tracefs = value;
            else if (key == "--trace" || key == "-t") {
                for (;;) {
                    const auto comma = value.find(',');
                    result.traces.emplace_back(value.substr(0, comma));
                    if (comma == string_view::npos)
                        break;
                    value.remove_prefix(comma + 1);
                }
            } else if (key == "--timeout") {
                unsigned number = 0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    number == 0)
                    throw invalid_argument("invalid disk timeout");
                result.timeout = number;
            } else
                throw invalid_argument("unknown disk option: " + string{key});
        }
        if (!result.help)
            validate_disk(result);
        return result;
    }
}
#endif // RAFTPP_BENCHMARK_DISK_OPTIONS_HPP
