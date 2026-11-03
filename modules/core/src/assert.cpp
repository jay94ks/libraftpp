// Optional assertion backtraces from cowsql/raft src/assert.h; see LICENSE.md.
#include "assert.hpp"
#if RAFTPP_WITH_BACKTRACE && !defined(NDEBUG)
#include <algorithm>
#include <backtrace.h>
#include <cstdio>
#include <cstdlib>
namespace raftpp::detail {

    namespace {

        struct backtrace_context {
            unsigned depth = 0;
        };

        void backtrace_error(void*, const char* message, int code) noexcept {
            std::fprintf(stderr, "backtrace error: %s (%d)\n", message ? message : "unknown", code);
        }

        int backtrace_frame(void* data, uintptr_t address, const char* file, int line,
                            const char* function) noexcept {
            auto& context = *static_cast<backtrace_context*>(data);
            std::fprintf(stderr, "#%u 0x%llx %s\n", context.depth++,
                         static_cast<unsigned long long>(address), function ? function : "unknown");
            if (file)
                std::fprintf(stderr, "    %s:%d\n", file, line);
            return 0;
        }

    }

    [[noreturn]] void assertion_failed(string_view expression, source_location location) noexcept {
        const auto length = static_cast<int>(
            std::min(expression.size(), static_cast<size_t>(numeric_limits<int>::max())));
        std::fprintf(stderr, "%s:%u: %s: Assertion `%.*s' failed.\nbacktrace:\n",
                     location.file_name(), static_cast<unsigned>(location.line()),
                     location.function_name(), length, expression.data());
        backtrace_context context;
        if (auto state = backtrace_create_state(nullptr, 1, backtrace_error, &context)) {
            (void)backtrace_full(state, 1, backtrace_frame, backtrace_error, &context);
        }
        std::fflush(stderr);
        std::abort();
    }

}
#endif
