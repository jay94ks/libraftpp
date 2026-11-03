# Errors and Owning Diagnostics

[한국어](../ko/errors.md) · [Node API](node.md) · [libuv I/O](uv_io.md)

Include `<raftpp/error.hpp>` or `<raftpp.hpp>` and link `raftpp::raftpp`. Error codes and bounded diagnostic values are available with every feature configuration.

## Codes and Messages

`errc` defines the numeric Raft status values. `make_error_code(errc)` returns a standard `error_code` using `raft_category()`, whose name is `raft`. The standard error-code-enum specialization permits comparison such as `status == errc::not_leader`.

`error_message(errc) noexcept` returns a static `string_view`, without allocation. Error values 1 through 23 use the C reference messages, including `resource name too long` and `operation can't be performed at this time`. Success has the C++ message `success`; unknown values have `unknown error`. Standard `error_code::message()` returns an owning string and can allocate.

## Diagnostic Values

```cpp
struct error_info {
    error_code code;
    error_code cause;
    array<char, 256> message;
    string_view text() const noexcept;
};
error_info make_error_info(error_code code, string_view operation = {},
                           string_view resource = {}, error_code cause = {}) noexcept;
error_info wrap_error_info(error_info info, string_view prefix) noexcept;
```

`error_info` owns its standard error codes and fixed array. Copying, construction through these helpers, wrapping, and `text()` allocate no storage. `text()` borrows this value's array; store the value before retaining its view. Copies survive callback return, backend destruction, and node shutdown. A default value denotes no error.

`make_error_info` composes operation, resource, and a Raft message. Nonempty contexts are separated with `: `. A supplied cause remains a separate standard `error_code`; text adds its category name and numeric value, without allocating its descriptive message. Foreign top-level codes similarly retain their category/value. Call `cause.message()` separately if an owning system description is needed and allocation is acceptable. Success produces an empty diagnostic.

Helpers write at most 255 bytes plus NUL. Wrapping follows the reference C truncation boundary: prefixes of 253 bytes or more omit the separator/tail; shorter prefixes retain the tail that fits. An explicitly empty prefix passed to `wrap_error_info` produces `: ` before an error. Truncation affects display only; codes/causes remain intact. The public array is a standard value; if modified without a terminator, `text()` still searches only within its bound. Diagnostic text is for people, not a stable parsing protocol, and byte truncation may split multibyte text.

## Backend and Node Contracts

`io::error_details() const noexcept` is an optional virtual query returning an owning diagnostic; its default is empty. A custom backend publishes the corresponding information before returning an immediate failure or invoking its failure completion. Getters must not allocate, throw, or reenter the node. Serialize access on the owning event-loop thread. Empty or mismatched backend information falls back to the operation's Raft code.

`node::error_details() const noexcept` copies the recorded node diagnostic. Asynchronous I/O completions snapshot backend information at callback entry, before queued processing or later backend updates. Immediate load, metadata, truncate, and submission failures also capture the backend context. The failure that initiates shutdown remains available even when cleanup or outer startup wrappers report more errors. A moved-from node returns a shutdown diagnostic. `node::error_message()` makes an owning string from the recorded text and can allocate; `error()` remains the Raft status query. Bootstrap/recovery backend failures record context without initiating shutdown. Other immediate validation/admission errors are returned by their operation and need not become a recorded node failure.

Constructor initialization failures throw `system_error` with the original status and matching backend text when supplied. Allocation during construction/exception formatting follows standard exception rules. An I/O error does not imply rollback or withdrawal of an accepted command.

With UV enabled, `uv_io::error_details()` returns recorded backend diagnostics. Directory-stat failures preserve the path and POSIX cause (for example `std::errc::no_such_file_or_directory`). File-handle operations can capture an owning native cause through their internal diagnostic output. A diagnostic may contain only an operation/code and an empty cause; do not assume every backend or native error supplies system details. Cancellation and cleanup still follow the [node](node.md) and [UV](uv_io.md) completion contracts.

## TCP Diagnostics

`transport::error_details() const noexcept` is an optional virtual query defaulting to an empty diagnostic. Existing custom transports need no override. Implementations publish matching request information before a failure return or completion. `tcp_transport::error_details()` returns an owning value containing the operation, peer ID, configured peer address, and available native cause. Before reading an incoming sender's handshake, its ID can be zero and the address can describe the local endpoint. Query and copy operations allocate nothing and run on the event-loop thread.

TCP libuv causes retain the `libuv` category and original negative status value. Values corresponding to Linux errno map to standard error conditions, allowing `info.cause == std::errc::connection_refused`. Libuv-specific values such as `UV_EAI_*`, `UV_EOF`, and `UV_UNKNOWN` retain that category rather than masquerading as errno. Synchronous listener `getaddrinfo` failures use the `getaddrinfo` category and that function's code; `EAI_SYSTEM` retains the captured errno in `generic_category`. Call `cause.message()` separately when an owning descriptive string is needed; it can allocate.

Connection attempts own their diagnosis and copy it into each send evicted from the pending queue. Write requests also own their result and peer context until completion. Copy information at callback entry: another request or a reentrant call can change subsequent queries, while existing copies survive connection/transport cleanup. Cancellation and new validation failures do not inherit earlier DNS/write causes. Nodes use the information captured by UV in their existing I/O failure handling; do not assume a send failure itself shuts down the node.

Diagnostics are observations, not replacements for `last_error()` or operation results. An accepted send can succeed after failed connection attempts. Immediately rejected sends and observed normal EOF do not change the existing `last_error()` value. EOF is queryable as `io_error`/`UV_EOF` context while retaining ordinary connection-close status/retry/completion behavior. Diagnostic text is not a stable parsing format. Follow the [TCP transport contracts](tcp_transport.md).

## Disk Diagnostics

With UV enabled, metadata, snapshot, and segment failures retain the failing file or directory path, operation, and available POSIX cause. The native cause remains separate from the Raft status: for example, failure to open a metadata slot can report `io_error` with `std::errc::is_a_directory`. Paths and operation text obey the same bounded-text rules as other diagnostics.

Internal synchronous disk operations accept an optional `error_info*` output. Each invocation writes its own result rather than a shared or thread-local last error. Success clears that output, including successful snapshot fallback and expected missing metadata slots. Concurrent workers must provide separate outputs and obey the disk operation's existing synchronization requirements. Snapshot and log coordinators retain each worker's diagnostic until its completion callback; metadata failures propagate through `uv_io` to the node.

Best-effort cleanup does not replace the failure that caused cleanup. A subsequent independent failure gets its own context rather than inheriting a previous native cause. Validation failures and failures without an available system error may have an empty cause. Unsupported capabilities accepted by filesystem probing remain successful results with empty diagnostics; they are not I/O failures. These diagnostics do not change durability, recovery, or rollback guarantees.

Filesystem probing publishes capabilities after successful AIO-context cleanup. If `io_destroy` fails during normal completion or unsupported-capability classification, return `io_error`, preserve the existing capability output, and retain the cleanup operation/directory/POSIX cause in diagnostics. Native cleanup `ENOMEM` also maps to `io_error`. When an earlier stage such as submission has already failed, retain that failure/cause and perform best-effort cleanup.

## Native Writer Diagnostics

With UV enabled, native writer failures retain their operation, descriptor number, offset, and available POSIX cause. Write-result diagnostics also include the reported result and expected byte count. Log-level diagnostics add the `open-N` segment name. Snapshot worker failures identify preparation, publication, or retrieval and include an available snapshot index. Text remains bounded as described above.

| Native observation | Returned/completion code | Diagnostic cause |
| --- | --- | --- |
| Negative write result, such as `-EIO` | `io_error` | Matching positive POSIX code in `generic_category`. |
| Negative write result `-ENOSPC` | `io_error`, preserving the writer's status mapping | `std::errc::no_space_on_device`. |
| Short nonnegative write result | `no_space` | Empty: the reported byte count describes the failure. |
| AIO initialization limit, `-EAGAIN` | `too_many` | `std::errc::resource_unavailable_try_again`. |
| Immediate native submission failure | Existing mapped Raft error; no completion callback | Captured native cause when representable. |

Each worker writes only its own request's diagnostic. The owning event loop publishes it before the corresponding completion callback. Capture `error_details()` at callback entry if using backend APIs directly; another operation or a reentrant call may replace the backend's stored information. The node captures completion information automatically. Snapshot completion diagnostics take priority over an earlier writer failure during that callback, including cancellation; between callbacks a coordinator can retain its fatal writer context. Diagnostic copies own their text and codes after requests/native handles are released; a recorded descriptor number describes that past operation and does not keep a file open.

Retries preserve their existing completion and shutdown behavior. A successful retry can retain the earlier observed diagnostic, while the operation completes successfully and `last_error()` retains its existing status semantics. Use the operation result and `node::error()` to determine its outcome; `error_details()` provides failure context, including observed transient failures. Native shutdown and cleanup capture their own failures without releasing outstanding request buffers early.

```cpp
#include <raftpp/node.hpp>

raftpp::error_info inspect_failure(const raftpp::node& node) noexcept
{
    auto info = node.error_details();
    if (info.code) {
        // info.text() borrows info; info.cause is a standard error_code.
        return raftpp::wrap_error_info(info, "application");
    }
    return info;
}
```
