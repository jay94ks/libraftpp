#ifndef RAFTPP_DETAIL_TRACING_HPP
#define RAFTPP_DETAIL_TRACING_HPP

#include <raftpp/tracing.hpp>
#include <algorithm>
#include <cstdio>

namespace raftpp::detail {
    // Standard printf formatting preserves C diagnostic text and its bound.
    // Stack storage replaces C's shared static scratch buffer.
    template <typename... Args>
    void logf(const tracer& tracing, trace_level level, source_location location,
              const char* format, Args... args) noexcept {
        if (!tracing.enabled())
            return;
        if constexpr (sizeof...(Args) == 0)
            tracing.log(level, format, location);
        else {
            array<char, 1024> text{};
            const int count = std::snprintf(text.data(), text.size(), format, args...);
            if (count < 0)
                return;
            tracing.log(level, {text.data(), std::min(static_cast<size_t>(count), text.size() - 1)},
                        location);
        }
    }
}
#endif // RAFTPP_DETAIL_TRACING_HPP
