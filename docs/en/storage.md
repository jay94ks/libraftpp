# Buffers, Entries, and Snapshots

[한국어](../ko/storage.md) · [Nodes, I/O, and State Machines](node.md) · [Memory Resources](memory.md)

Include `<raftpp.hpp>` and link the CMake target `raftpp::raftpp`. These APIs use C++20 and are available regardless of `RAFTPP_WITH_UV`, `RAFTPP_WITH_LZ4`, or `RAFTPP_WITH_FIXTURE`. All names are in namespace `raftpp`.

## Buffer Ownership

| Type | Storage and copying | Access |
| --- | --- | --- |
| `buffer` | Exclusive ownership through `vector<byte, resource_allocator<byte>>`. Movable, noncopyable. | Mutable and read-only byte views. |
| `aligned_buffer` | Exclusive ownership with specified alignment. Movable, noncopyable. | Mutable and read-only byte views. |
| `shared_buffer` | Shared ownership of the entire allocation through standard `shared_ptr`. Copying does not duplicate bytes. | Read-only ranges and slices sharing the allocation. |

`buffer(size_t size, resource_allocator<byte> = {})` and `aligned_buffer(size_t size, size_t alignment, resource_allocator<byte> = {})` initialize bytes to zero. Both retain their allocation resource and expose `get_allocator()`. Default allocators capture the library selection; explicit allocators choose a resource directly. See [memory resources](memory.md) for selection, move/swap propagation, and lifetime. Alignment must be a power of two and at least `alignof(max_align_t)`. Invalid alignment throws `invalid_argument`; allocation failure throws `bad_alloc`. Ordinary `buffer` construction, copying, and resizing can throw the standard container exceptions `bad_alloc` or `length_error`.

`buffer` provides `size()`, `empty()`, `data()`, `bytes()`, `resize(size_t)`, `clear()`, and `swap(buffer&)`. `buffer::copy(view<const byte>)` creates an independent byte copy. Resizing or destroying the owner can invalidate existing pointers and views.

`aligned_buffer` provides `size()`, `empty()`, `alignment()`, `data()`, `bytes()`, and `swap(aligned_buffer&)`. Its size is fixed at construction; a moved-from object's `size()` is zero. Moving or swapping also transfers the matching aligned deallocation method.

```cpp
explicit shared_buffer(buffer storage);
explicit shared_buffer(aligned_buffer storage);
static shared_buffer copy(view<const byte> input, resource_allocator<byte> allocator = {});
shared_buffer slice(size_t offset, size_t count = std::dynamic_extent) const;
```

Pass an owning buffer with `std::move`. These constructors take ownership and can throw `bad_alloc` when allocating the shared control block. The moved argument's storage is cleaned up automatically even if construction fails. `copy` allocates independent storage and can throw `bad_alloc` or `length_error`.

For `slice`, `offset` counts bytes from the current range's beginning; omitting `count` selects the remainder. It throws `out_of_range` if `offset > size()` or `count > size() - offset`. Empty slices are allowed. A slice extends the entire allocation's lifetime without allocating additional bytes.

`shared_buffer::bytes()` returns `view<const byte>`. The view itself does not extend storage lifetime; retain the original or another owning copy while using it. `storage()` returns `const shared<const byte>&`; copying this handle by value retains storage. Its pointer may address the start of a slice and must not be freed directly. `shares_storage_with(const shared_buffer&)` checks whether both ranges own the same allocation. Two default-constructed empty objects own no allocation and return `false`.

Moving a `shared_buffer` leaves the source empty. Shared ownership does not synchronize concurrent byte access. Do not modify shared bytes through mutable aliases retained before ownership transfer.

```cpp
#include <raftpp.hpp>

raftpp::shared_buffer make_payload()
{
    raftpp::aligned_buffer storage{4096, 4096};
    storage.bytes()[0] = raftpp::byte{42};
    raftpp::shared_buffer batch{std::move(storage)};
    return batch.slice(0, 1); // The returned range owns the entire aligned allocation.
}
```

## Entries

```cpp
enum class entry_type : uint8_t { command = 1, barrier, change };

struct entry {
    raftpp::term term = 0;
    entry_type type = entry_type::command;
    shared_buffer payload;
};

error_code copy_entry(const entry& input, entry& output) noexcept;
error_code copy_entries(view<const entry> input, vector<entry>& output) noexcept;
```

Ordinary `entry` value copying copies metadata and shares payload ownership. `copy_entry` also deep-copies the bytes. `copy_entries` packs all bytes into one allocation in input order without padding and gives each output entry shared ownership of its slice. Retaining one output entry retains the entire batch. Empty input clears the output vector.

Both functions return an empty `error_code` on success, `errc::out_of_memory` on allocation failure, or `errc::too_big` for an unrepresentable size or container length. Output remains unchanged on error. Input and output may be the same object, and an input view may refer to the output vector. These copy functions do not validate the protocol validity of entry terms or types.

## Snapshots

```cpp
struct snapshot {
    log_index index = 0;
    raftpp::term term = 0;
    raftpp::configuration configuration;
    log_index configuration_index = 0;
    vector<shared_buffer> buffers;
};

error_code copy_snapshot(const snapshot& input, snapshot& output) noexcept;
```

`index` and `term` identify the last included entry; `configuration_index` identifies the configuration entry. Ordinary value copying copies the configuration by value and shares ownership of each payload. `copy_snapshot` deep-copies the configuration and bytes, concatenating all input buffers into one output buffer in order. Empty input still produces one zero-length output buffer. The function does not validate the protocol validity of indices or terms.

It returns an empty `error_code` on success, `errc::out_of_memory` on allocation failure, or `errc::too_big` for size or length limits. Output remains unchanged on error, and input and output may be the same object. Values and shared ownership manage data lifetime; no separate close or free call is required.
