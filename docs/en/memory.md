# Memory Resources and Ownership

[한국어](../ko/memory.md) · [Buffers](storage.md) · [Nodes](node.md) · [Native I/O](uv_io.md) · [Fixture](fixture.md)

Include `<raftpp/memory.hpp>` and link `raftpp::raftpp`. These APIs use standard C++20 PMR and smart pointers and do not depend on UV, LZ4, or fixtures. `pmr` in `common.hpp` aliases `std::pmr`.

## Standard Aliases Expressing Ownership

`<raftpp/common.hpp>` provides the following aliases. They add no separate pointer class or reference-counting implementation and retain the corresponding standard type's construction, conversion, move, and destruction contracts.

| Alias | Corresponding type and ownership |
| --- | --- |
| `owned<T, Deleter = std::default_delete<T>>` | `std::unique_ptr<T, Deleter>`. Exclusive ownership, transferred by moving. Use a matching deleter for native resources. |
| `unique<T, Deleter = std::default_delete<T>>` | A compatibility name for the same type as `owned<T, Deleter>`. |
| `shared<T>` | `std::shared_ptr<T>`. Copying an owning handle shares the target's lifetime. |
| `weak<T>` | `std::weak_ptr<T>`. Non-owning; retain the owner returned by `lock()` while accessing the target. |
| `borrowed<T>` | `T*`. A nullable non-owning pointer that does not extend lifetime. |
| `ref<T>` | `std::reference_wrapper<T>`. Refers to a live lvalue, accessed with `get()`. Its lifetime must be guaranteed separately. |
| `view<T, Extent = std::dynamic_extent>` | `std::span<T, Extent>`. A non-owning contiguous-memory view; use `view<const T>` for read-only access. |

Copying or capturing `borrowed`, `ref`, or `view` in a callback does not keep its target alive. For asynchronous calls, capture the required owning values or pass the owning buffers required by that operation. Shared lifetime through `shared` does not synchronize concurrent target access. Add an owning handle around a value that already owns its data, such as `buffer`, only when lifetime sharing is needed.

`array`, `vector`, `deque`, `list`, `map`, `string`, `string_view`, `optional`, `variant`, `function`, error types, and common type traits are also exposed through standard-type `using` declarations. Their standard template parameters and operations remain available. `chrono`, `pmr`, and `fs` alias the namespaces `std::chrono`, `std::pmr`, and `std::filesystem`, respectively.

## Resource Selection

| Function | Contract |
| --- | --- |
| `get_memory_resource() noexcept` | Return a shared handle to the library's selected `pmr::memory_resource`. The default is standard `new_delete_resource()`. |
| `set_memory_resource(shared<pmr::memory_resource>) noexcept` | Atomically replace the selection and return the previous resource. Null selects the built-in default. |
| `reset_memory_resource() noexcept` | Restore the built-in default. |

Selection applies when creating an allocator, buffer, or allocator-aware library object. Existing allocations keep their originating resource. Resizing an existing buffer uses its original allocator, and resetting the selection never changes its deallocation route. This library selection is separate from `std::pmr::set_default_resource` and does not replace process-wide `operator new` or libuv/LZ4 allocators. The static built-in resource needs no allocated shared control block.

The built-in handle aliases a static resource without an owning control block: it has a nonnull pointer and zero `use_count`, and a weak handle to it is expired. This does not describe the static resource's lifetime. Custom resources supplied with an owning shared pointer retain normal shared/weak ownership semantics.

## Buffers with Automatic Resource Lifetime

```cpp
#include <raftpp/buffer.hpp>

raftpp::shared_buffer make_payload()
{
    auto resource = std::make_shared<
        raftpp::pmr::synchronized_pool_resource>();
    raftpp::buffer bytes{4096,
        raftpp::resource_allocator<raftpp::byte>{resource}};
    bytes.bytes()[0] = raftpp::byte{42};
    return raftpp::shared_buffer{std::move(bytes)};
}
```

The returned value retains the pool without requiring a separate resource owner in the caller. A `shared_buffer` slice retains the same payload and resource; its standard shared control block also retains the resource until the last weak reference is released, even after payload destruction.

`buffer(size, resource_allocator<byte> = {})` and `buffer::copy(bytes, allocator = {})` select an explicit or current resource. An allocator-only constructor creates an empty buffer. `get_allocator()` returns its owning allocator. `resize` preserves the old prefix, initializes new bytes to zero, and preserves the buffer on allocation failure. `clear` keeps capacity like a standard vector. Move assignment and swap propagate the allocator along with storage, including between different resources; moved-from buffers remain reusable.

`aligned_buffer(size, alignment, resource_allocator<byte> = {})` requires power-of-two alignment at least `alignof(max_align_t)`. Sizes need not be multiples of alignment. Storage is zero initialized; the deleter retains the exact resource, size, and alignment. `get_allocator()` exposes the owning allocator. Move/swap preserve matching allocation metadata. `shared_buffer::copy(bytes, allocator = {})` supports the same resource selection. Construction may throw `bad_alloc`, `length_error`, or alignment/argument exceptions; fallible library operations retain their documented `error_code`/output contracts.

## Standard Allocator and Object Factories

As required by the standard Allocator contract, `T` is a cv-unqualified object type. To create a const object, use `std::allocate_shared<const T>(resource_allocator<T>{}, args...)` with a non-const allocator value type.

`resource_allocator<T>` satisfies the standard Allocator interface and delegates raw allocation/deallocation to `std::pmr::polymorphic_allocator<T>`. Its additional state owns the resource through `shared_ptr`, which a plain polymorphic allocator does not. It supports rebinding, allocator traits, and standard containers. Copy assignment, move assignment, and swap propagate the resource. Moving an allocator preserves a usable resource in its source. Equality compares originating resource pointers. Counts exceeding the standard allocator's size bound throw `bad_array_new_length`.

```cpp
#include <raftpp/memory.hpp>

raftpp::shared<int> make_value()
{
    auto resource = std::make_shared<
        raftpp::pmr::synchronized_pool_resource>();
    raftpp::resource_allocator<int> allocator{resource};
    auto unique = raftpp::allocate_owned<int>(allocator, 42);
    raftpp::resource_vector<int> values{allocator};
    values.push_back(*unique);
    return std::allocate_shared<int>(allocator, values.front());
}
```

Use standard `std::allocate_shared<T>` for shared objects. `allocate_owned<T>(resource_allocator<T>, args...)` covers scalar owning allocation through standard PMR `new_object`/`delete_object`. It returns `resource_owned<T>`, an alias of `unique_ptr<T, resource_deleter<T>>`, and releases storage if construction throws. The deleter keeps the original type/resource; arrays are unsupported. Converting that unique owner to a compatible `shared<Base>` retains the original deleter. Do not manually free its pointer through a different allocator.

## Allocator-Aware Standard Containers

`common.hpp` provides the resource selection functions, `resource_allocator`, and the following aliases. Include `memory.hpp` for the owning object factory and deleter as well. Each alias preserves the corresponding standard container's template parameters and operations.

| Alias | Standard type and defaults |
| --- | --- |
| `resource_vector<T, Allocator = resource_allocator<T>>` | `std::vector<T, Allocator>` |
| `resource_deque<T, Allocator = resource_allocator<T>>` | `std::deque<T, Allocator>` |
| `resource_list<T, Allocator = resource_allocator<T>>` | `std::list<T, Allocator>` |
| `resource_map<Key, Value, Compare = std::less<Key>, Allocator = resource_allocator<pair<const Key, Value>>>` | `std::map<Key, Value, Compare, Allocator>` |
| `resource_basic_string<Char, Traits = std::char_traits<Char>, Allocator = resource_allocator<Char>>` | `std::basic_string<Char, Traits, Allocator>` |
| `resource_string` | `resource_basic_string<char>` |

A default allocator captures the library selection at container construction. Later growth keeps that allocator. Copy construction retains the source allocator; copy/move assignment and swap propagate it. Each resource object must remain suitable for all uses of its containers. Standard allocator preconditions still apply: lists exchanged with `splice` must have equal allocators. Containers and strings with different allocator types are distinct types; use views or explicit value copies at those boundaries.

## Allocation Boundaries and Concurrency

Library byte buffers, aligned staging/receive buffers, shared payload control blocks, allocated entry handles, retained tracer/callback objects, node request/state objects, and native disk/file/TCP request/state objects use these resources. Log-cache/reference-index and term-trail storage, core addresses, candidate votes, leader progress, rollback backups, and outgoing core message arrays also use resource-aware standard containers. Fixture state/request objects and its pending-request/leader-log containers also use the selected resources. Node request/operation/event/close queues, native writer request/buffer/iovec/completion arrays, log-writer segment/preparation/truncation/barrier queues, snapshot-get and UV-send queues, and TCP listener/peer/incoming/send queues, stream-buffer descriptors, and internal addresses use them too.

Each default allocator captures the selection at its own creation; an existing node does not pin selection for future request objects, role states, or event updates. Growing an existing queue still allocates through its original resource, including when inserting a newly created request with a different resource. Spliced queue pairs and log-writer barrier-request groups explicitly share an allocator with the corresponding originating queue; temporary completion/cancellation lists retain it as well. Changing the selection does not migrate existing storage. A closed handle can retain empty containers and their resource owners until that handle is destroyed. Set the selection before the operations to which it should apply, or supply explicit allocators for values.

Ordinary `vector`/`string`/`list` aliases keep their standard default allocator unless a resource allocator is explicitly supplied. Public entry batches, message addresses, configuration/snapshot value containers, and file-list results retain that default. Selecting a library resource does not redirect their backing allocations, `std::function` boxing, filesystem paths, exception storage, user-created `std::make_shared` objects, or external-library internals. Standard containers used as members do not automatically inherit an enclosing object's allocator. Resource ownership covers the resource object; borrowed upstream resources or external arenas must also remain valid according to their standard PMR contracts.

The selection is atomic; allocator/resource access is not automatically serialized. Native file workers can allocate concurrently with the loop. Use a thread-safe resource, such as `synchronized_pool_resource`, for those paths, or otherwise guarantee appropriate synchronization. `do_allocate` must return correctly sized/aligned storage or throw `bad_alloc`; `do_deallocate` must not throw. A shared pointer constructed with a no-op deleter still borrows its target; supply real ownership when automatic lifetime is required.
