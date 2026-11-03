// Independent C++ port of cowsql/raft tracing.c and tracing.h; see LICENSE.md.
#include <raftpp/tracing.hpp>
#include <raftpp/memory.hpp>
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

namespace raftpp {
    namespace {
        atomic_bool stderr_enabled{false};
        thread_local bool emitting = false;
    }
    tracer::tracer(trace_callback callback) : stderr_(false) {
        if (callback)
            callback_ = std::allocate_shared<const trace_callback>(
                resource_allocator<trace_callback>{}, std::move(callback));
    }
    tracer tracer::noop() noexcept {
        tracer result;
        result.stderr_ = false;
        return result;
    }
    bool tracer::enabled() const noexcept {
        return callback_ || (stderr_ && stderr_tracing_enabled());
    }
    void tracer::emit(uint32_t type, borrowed<const void> info) const noexcept {
        if (emitting)
            return;
        auto callback = callback_;
        if (!callback &&
            (!stderr_ || !stderr_tracing_enabled() || type != trace_diagnostic || !info))
            return;
        emitting = true;
        if (callback) {
            const trace_event event{type, info};
            try {
                (*callback)(event);
            } catch (...) { /* Tracing is observational. */
            }
        } else {
            const auto& diagnostic = *static_cast<const diagnostic_trace*>(info);
            const auto ns = chrono::duration_cast<chrono::nanoseconds>(
                                chrono::system_clock::now().time_since_epoch())
                                .count();
            const auto text = diagnostic.message.substr(0, 1023);
            std::fprintf(stderr, "LIBRAFT   %" PRId64 " %s:%u %.*s\n", static_cast<int64_t>(ns),
                         diagnostic.location.file_name(), diagnostic.location.line(),
                         static_cast<int>(text.size()), text.empty() ? "" : text.data());
        }
        emitting = false;
    }
    void tracer::log(trace_level level, string_view message,
                     source_location location) const noexcept {
        if (!enabled())
            return;
        const diagnostic_trace diagnostic{level, message.substr(0, 1023), location};
        emit(trace_diagnostic, &diagnostic);
    }
    void maybe_enable_tracing(bool enabled) noexcept {
        if (std::getenv("LIBRAFT_TRACE") != nullptr)
            stderr_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool stderr_tracing_enabled() noexcept {
        return stderr_enabled.load(std::memory_order_relaxed);
    }
}
