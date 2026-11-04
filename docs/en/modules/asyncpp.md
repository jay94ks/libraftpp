# asyncpp Module API

[한국어](../../ko/modules/asyncpp.md) · [Modules](index.md)

`asyncpp_executor final : executor` submits callbacks through `asyncpp::dispatcher::push`. Select `RAFTPP_MODULE_ASYNCPP=ON`, link `raftpp::asyncpp`, and include `<raftpp/asyncpp/asyncpp.hpp>`. Depends on core and Threads; dispatcher headers come from the checkout or an installed SDK. See [dependencies](../dependencies.md) for SDK preparation.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asyncpp)
target_link_libraries(app PRIVATE raftpp::asyncpp)
```

## Construction and Ownership

```cpp
explicit asyncpp_executor(shared<asyncpp::dispatcher> dispatcher);
error_code post(function<void()> callback) noexcept override;
```

Construction shares the dispatcher. Null throws `std::invalid_argument("null asyncpp dispatcher")`. The upstream `dispatcher` has no public virtual destructor: create an owner using `std::make_shared<ConcreteDispatcher>()` and convert it when passing the argument. Do not create a separate owner from a borrowed pointer.

Accepted callbacks also capture the dispatcher owner. Destroying `asyncpp_executor` does not destroy accepted queued work or its dispatcher before execution finishes. Destruction does not drain/cancel/join the queue. Queued work retaining its own dispatcher also keeps that owner alive, so the caller must continue driving the queue.

## `post` Contract and Errors

| Result | Meaning |
| --- | --- |
| Empty `error_code` | Dispatcher accepted the callback for one later invocation. |
| `errc::invalid` | Empty callback. |
| `errc::out_of_memory` | Dispatcher submission threw `bad_alloc`. |
| `errc::io_error` | Dispatcher submission threw another exception. |

Immediate failure invokes no user callback. Keep a separate copy/owner if the caller needs to preserve a submitted callable's captures. User callbacks executed by the queue must not throw. `post` maps submission exceptions; it does not install a handler for exceptions thrown during later callback execution.

The dispatcher must enqueue work for later execution and must not later execute work whose submission reported failure. Allowed calling threads, ordering, and queue driving follow the concrete dispatcher. This adapter creates no extra thread, synchronization, or Raft callback forwarding.

## Example

```cpp
#include <raftpp/asyncpp/asyncpp.hpp>

raftpp::error_code increment_later(
    raftpp::shared<asyncpp::dispatcher> dispatcher,
    raftpp::shared<int> value)
{
    if (!value)
        return raftpp::errc::invalid;
    raftpp::asyncpp_executor scheduler{std::move(dispatcher)};
    return scheduler.post([value = std::move(value)] { ++*value; });
}
```

Supply a shared owner of the concrete dispatcher and a valid value owner, then drive the queue. The scheduler is destroyed when this function returns, while the captured value and dispatcher remain alive. Serialize other accesses to the same value within the dispatcher's execution domain. This executor is not automatically connected to node APIs; use it in application code needing callback submission. Also follow the [common executor contract](../streams.md).
