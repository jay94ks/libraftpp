# Cluster Configuration Values

[한국어](../ko/configuration.md)

`configuration` in `<raftpp/configuration.hpp>` is a synchronous value type providing an ordered server list and the existing Raft byte format. It works without UV/LZ4/fixture. Change node membership through the `add`, `assign`, and `remove` requests described in [node](node.md).

## Values and Ownership

```cpp
struct server {
    server_id id;
    string address;
    raftpp::role role;
};
```

Roles are `role::voter`, `role::standby`, or `role::spare`. A configuration owns its servers and addresses and starts empty. Copying makes independent server/address values; moving and destruction follow standard-container semantics. Copy construction/assignment can throw standard allocation exceptions. Explicit methods are `noexcept` and return the `error_code` or query results below.

Server/address storage follows ordinary `std::vector`/`std::string` allocator semantics. See [memory allocation](memory.md) for the scope of the library-selected resource.

`servers()` and `get()` are non-owning observations. Retain the configuration to keep their targets alive; copied views/pointers do not extend lifetime. Query pointers/views/indices again after successful addition/removal/decoding or assignment. Do not use old pointers/views after the configuration is destroyed.

## Modification

| Method | Behavior and errors |
| --- | --- |
| `error_code add(server_id id, string_view address, role role) noexcept` | Append a server. ID zero returns `bad_id`, an invalid role `bad_role`, and an embedded address NUL `invalid`. Empty addresses are allowed. Duplicate IDs/addresses return `duplicate_id`/`duplicate_address`. |
| `error_code remove(server_id id) noexcept` | Remove the server, preserving survivor order. Unknown IDs return `bad_id`. Removing the last server may leave an empty value. Does not allocate. |

`add` validates ID/role/NUL first, then checks duplicates in storage order. It copies the address, so the input string need not survive the call. Allocation failure returns `out_of_memory`; unrepresentable string/container length returns `too_big`. Errors preserve the previous configuration. Success is an empty `error_code`.

## Queries

| Method | Result |
| --- | --- |
| `size_t size() const noexcept`, `bool empty() const noexcept` | Server count and whether it is empty. |
| `view<const server> servers() const noexcept` | Read-only contiguous view in storage order. |
| `borrowed<const server> get(server_id id) const noexcept` | Server pointer or `nullptr`. |
| `size_t voter_count() const noexcept` | Number of voters. |
| `size_t index_of(server_id id) const noexcept` | Index in the full list, or `size()` if absent. |
| `size_t index_of_voter(server_id id) const noexcept` | Index counting only voters, or `size()` if absent/not a voter. |
| `size_t actual_index_of_voter(size_t index) const noexcept` | Full-list index corresponding to a voter index, or `size()` if out of range. |

## Encoding and Decoding

```cpp
size_t encoded_size() const noexcept;
error_code encode(view<byte> output) const noexcept;
error_code encode(buffer& output) const noexcept;
error_code decode(view<const byte> input) noexcept;
```

The format contains a version byte with value 1, little-endian `uint64_t` server count, then each server's little-endian ID/NUL-terminated address/one-byte role. Encoding rounds the total up to a multiple of eight and zeroes padding. `encoded_size()` returns the required size, or zero if unrepresentable. An empty configuration has a calculated size of 16, but `encode` returns `invalid`.

The view overload requires at least `encoded_size()` output bytes, otherwise returning `too_big`. Success zeroes from the logical data end through the supplied view's end. Supply an exactly sized view to specify the write range. The buffer overload prepares a new owning buffer and replaces output on success. Allocation failure returns `out_of_memory`, excessive length `too_big`. Both overloads preserve output on error.

`decode` copies servers/addresses and replaces the entire configuration on success. Truncated headers/IDs/NUL-terminated addresses/roles, unsupported versions, invalid IDs/roles and duplicate IDs/addresses return `malformed`. Allocation failure returns `out_of_memory`; errors preserve the previous configuration and input. Eight-byte input padding is optional; additional bytes after the declared servers are not interpreted. A valid zero-server header can decode to an empty configuration. Input storage may be released after successful decoding.

## Example

```cpp
#include <raftpp/configuration.hpp>

int main()
{
    raftpp::configuration members;
    if (auto error = members.add(1, "127.0.0.1:9001", raftpp::role::voter))
        return error.value();
    if (auto error = members.add(2, "127.0.0.1:9002", raftpp::role::standby))
        return error.value();

    raftpp::buffer bytes;
    if (auto error = members.encode(bytes)) return error.value();
    raftpp::configuration decoded;
    if (auto error = decoded.decode(bytes.bytes())) return error.value();
    return decoded.voter_count() == 1 && decoded.index_of_voter(2) == decoded.size()
        ? 0 : 1;
}
```
