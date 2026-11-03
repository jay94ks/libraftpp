#ifndef RAFTPP_TRACING_HPP
#define RAFTPP_TRACING_HPP

#include <raftpp/common.hpp>
#include <raftpp/export.hpp>

namespace raftpp {
    enum class trace_level { error = 1, warning, info, debug, trace };
    inline constexpr uint32_t trace_diagnostic = 1;
    inline constexpr uint32_t trace_write_submit = 1U << 8;
    inline constexpr uint32_t trace_write_complete = 2U << 8;
    struct diagnostic_trace {
        trace_level level = trace_level::trace;
        string_view message;
        source_location location;
    };
    struct trace_event {
        uint32_t type = 0;
        // Synchronous borrow: diagnostic_trace for trace_diagnostic, otherwise
        // event-specific data (null for the native writer's two events).
        borrowed<const void> info = nullptr;
    };
    using trace_callback = function<void(const trace_event&)>;

    // Default construction selects process-wide stderr tracing, initially off.
    // Copies share a custom callable; emit retains it through reentrant changes.
    class RAFTPP_API tracer {
    public:
        tracer() noexcept = default;
        explicit tracer(trace_callback callback);
        [[nodiscard]] static tracer noop() noexcept;
        [[nodiscard]] bool enabled() const noexcept;
        void emit(uint32_t type, borrowed<const void> info = nullptr) const noexcept;
        void log(trace_level level, string_view message,
                 source_location location = source_location::current()) const noexcept;

    private:
        shared<const trace_callback> callback_;
        bool stderr_ = true;
    };
    // Match C's process-global behavior: change the stderr switch only when
    // LIBRAFT_TRACE exists (including an empty value). Unset means no change.
    RAFTPP_API void maybe_enable_tracing(bool enabled) noexcept;
    [[nodiscard]] RAFTPP_API bool stderr_tracing_enabled() noexcept;
}
#endif // RAFTPP_TRACING_HPP
