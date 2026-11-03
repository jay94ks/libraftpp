#ifndef RAFTPP_BENCHMARK_OPTIONS_HPP
#define RAFTPP_BENCHMARK_OPTIONS_HPP

#include <raftpp/common.hpp>
#include <charconv>

namespace raftpp::benchmark {
    struct options {
        fs::path directory = ".";
        size_t buffer = 2048;
        uint64_t size = 8 * 1024 * 1024;
        unsigned timeout = 30000;
        string address = "127.0.0.1:0";
        bool help = false;
        uint64_t operations() const noexcept {
            return size / buffer;
        }
    };
    inline uint64_t bytes(string_view text) {
        if (text.empty())
            throw invalid_argument("empty numeric value");
        uint64_t multiplier = 1;
        const auto suffix = text.back();
        if (suffix == 'K' || suffix == 'M' || suffix == 'G') {
            multiplier = suffix == 'K' ? 1024 : suffix == 'M' ? 1024 * 1024 : 1024 * 1024 * 1024;
            text.remove_suffix(1);
        }
        uint64_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
            value > numeric_limits<uint64_t>::max() / multiplier)
            throw invalid_argument("invalid numeric value");
        return value * multiplier;
    }
    inline void validate_submit(const options& value) {
        if (value.buffer < 48 || value.buffer > 1024 * 1024 || value.buffer % 8 ||
            value.size == 0 || value.size > numeric_limits<uint32_t>::max() || value.size % 4096 ||
            value.size % value.buffer || value.operations() > numeric_limits<size_t>::max() ||
            value.address.empty() || value.timeout == 0)
            throw invalid_argument(
                "submit requires aligned entries of 48..1048576 bytes and a positive total divisible by 4096 and the entry size");
    }
    inline options parse_submit(view<const string_view> arguments) {
        options result;
        for (size_t i = 0; i < arguments.size(); ++i) {
            auto key = arguments[i];
            if (key == "--help" || key == "-?") {
                result.help = true;
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
                    throw invalid_argument("missing benchmark option value");
                value = arguments[i];
            }
            if (key == "--dir" || key == "-d")
                result.directory = value;
            else if (key == "--buf" || key == "-b") {
                const auto number = bytes(value);
                if (number > numeric_limits<size_t>::max())
                    throw invalid_argument("buffer size overflow");
                result.buffer = static_cast<size_t>(number);
            } else if (key == "--size" || key == "-s")
                result.size = bytes(value);
            else if (key == "--address")
                result.address = value;
            else if (key == "--timeout") {
                unsigned number = 0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    number == 0)
                    throw invalid_argument("invalid benchmark timeout");
                result.timeout = number;
            } else
                throw invalid_argument("unknown benchmark option: " + string{key});
        }
        if (!result.help)
            validate_submit(result);
        return result;
    }
}
#endif // RAFTPP_BENCHMARK_OPTIONS_HPP
