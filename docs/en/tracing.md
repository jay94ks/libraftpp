# Tracing

[한국어](../ko/tracing.md) · [Nodes](node.md) · [libuv I/O](uv_io.md) · [TCP](tcp_transport.md)

`<raftpp/tracing.hpp>` provides synchronous diagnostic and metric events independently of UV, LZ4, and fixture options. Link `raftpp::raftpp`. `source_location` in `common.hpp` aliases C++20 `std::source_location`.

## Observers

```cpp
#include <raftpp/tracing.hpp>
#include <iostream>

raftpp::tracer make_tracer()
{
    return raftpp::tracer{[](const raftpp::trace_event& event) {
        if (event.type != raftpp::trace_diagnostic || !event.info) return;
        const auto& value = *static_cast<
            raftpp::borrowed<const raftpp::diagnostic_trace>>(event.info);
        std::clog << value.location.file_name() << ':'
                  << value.location.line() << ' ' << value.message << '\n';
    }};
}
```

`tracer(trace_callback)` retains a callable with standard shared ownership; copies share it. Construction and conversion to `std::function` can throw allocation exceptions. An empty callback or `tracer::noop()` disables tracing; no-op construction allocates nothing. Default construction selects the process-wide stderr tracer.

| Method | Contract |
| --- | --- |
| `enabled() noexcept` | Whether a custom observer or enabled stderr sink is selected. |
| `log(trace_level, string_view, source_location = source_location::current()) noexcept` | Emit a diagnostic, truncating to 1023 bytes. The default location captures the caller. |
| `emit(uint32_t type, borrowed<const void> info = nullptr) noexcept` | Synchronously emit an event whose data format depends on its code. |

Copies, internal formatting, and emission allocate no library storage. Exceptions from observers are suppressed and do not change operation results or initiate shutdown. The active callable survives replacement during emission. Nested tracing on the same thread is suppressed, including through another tracer. Observers must not mutate or reenter the observed node/core/I/O; replacing its tracer through `set_tracer` is permitted. Follow the observed object's loop/thread contract; shared ownership does not synchronize captures across loops.

## Event Data and Lifetime

| Code | Data during emission |
| --- | --- |
| `trace_diagnostic` = 1 | `info` points to `diagnostic_trace`, containing `trace_level level`, `string_view message`, and `source_location location`. |
| `trace_write_submit` = 256 | Null data. Before native file-write submission; a submission failure may have no matching completion. |
| `trace_write_complete` = 512 | Null data. Before an accepted native write's completion callback, including cancellation. |
| Application code | Caller-defined data. Codes 1–255 are reserved for core events, 256–65535 for backends; applications use 65536 onwards. |

Levels preserve C numbers: error 1, warning 2, info 3, debug 4, trace 5. Custom observers receive all levels and can filter them. The event, `info`, and diagnostic message are synchronous borrows valid only during emission. Copy the message into `string` and retain a source-location value for stored records or asynchronous logging. Copying the event/diagnostic alone does not own its message. Application data follows the same borrowing contract unless separately retained by the emitter.

## Object Configuration

Set `node_options::tracing`, `uv_io_options::tracing`, and `tcp_transport_options::tracing` at construction. Node, file/timer backend, and TCP are independent channels; copy the same tracer into each to collect all events. Nodes do not replace backend observers, and `uv_io` does not replace transport observers.

Each of these classes provides `set_tracer(tracer) noexcept`. The UV setter propagates to the file coordinator and existing/future native segment writers, including pending completions. Internal `detail::file_writer` also supports `writer_options::tracing` and `set_tracer`. Replacement during a write can send submit/completion to different observers. Configuration retains its callable until replacement or object destruction; close drains operations without clearing configuration.

Core diagnostics cover startup, all event types, role/term changes, vote decisions, timeout decisions, and state/update summaries. Node errors include operation context and numeric status. UV/TCP diagnostics describe lifecycle, file writes, snapshot preparation/publication, messages, connections, and retry queues. Diagnostic text and C++ source paths are observational, not stable parsing interfaces. Native writer event codes preserve C values.

## Default stderr

The default sink starts disabled. `maybe_enable_tracing(bool) noexcept` changes its process-wide atomic switch only when `LIBRAFT_TRACE` exists, regardless of its value, including empty or `0`. An absent variable leaves the switch unchanged. Query it with `stderr_tracing_enabled() noexcept`. Node construction calls `maybe_enable_tracing(true)`, matching C `raft_init`; standalone backends can call it explicitly. Configure environment variables before concurrent library use.

stderr prints all diagnostic levels with nonnull data as `LIBRAFT   <realtime nanoseconds> <file>:<line> <message>` and a newline. It ignores writer/application events. Time comes from `std::chrono::system_clock`; formatting uses standard C I/O and independent stack buffers. No-op/custom tracers are unaffected by the stderr switch.

## Assertion Call Stacks

`RAFTPP_WITH_BACKTRACE` is a default-OFF CMake option. When ON, configuration checks for GNU libbacktrace headers/library and `__assert_fail` for assertion output. Independent of UV/LZ4/fixture; OFF does not require or link libbacktrace. Dependencies needed for static linkage also propagate through `raftpp::raftpp`.

`RAFTPP_WITH_BACKTRACE` in `<raftpp/config.hpp>` is 0 or 1; `raftpp::features::backtrace` is an `inline constexpr bool` expressing the same selection. The flag describes the build selection. If the library defines `NDEBUG`, assertions are neither evaluated nor backtraced even with the option ON. Typical Debug builds enable assertions; Release, RelWithDebInfo, and MinSizeRel disable them. Actual behavior follows the library's compilation flags.

When an active assertion fails, print its expression, source location, function, and call frames to stderr, then terminate the process with `abort()`. Address/symbol/file/line resolution depends on the binary and debug information; failure to collect a stack still terminates the assertion. libbacktrace may allocate its own memory, and this failure path does not use the user PMR resource. Diagnostic text and frame formatting are not stable parsing interfaces.

This behavior is independent of `LIBRAFT_TRACE`, the stderr tracing switch, and custom `tracer` objects. It installs no global signal handler and does not translate failure into an exception or error code. With the option OFF, standard C++ assertion behavior applies. The option does not make public API precondition violations recoverable errors.
