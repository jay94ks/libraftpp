# Zstandard Module API

[한국어](../../ko/modules/zstd.md) · [Modules](index.md)

`zstd_codec final : compression_codec` synchronously compresses/decompresses a Zstandard frame. Select `RAFTPP_MODULE_ZSTD=ON`, link `raftpp::zstd`, and include `<raftpp/zstd/zstd.hpp>`. Depends on core and `zstd` CONFIG package providing `zstd::libzstd_shared` or `zstd::libzstd_static`. UV is not required. See [dependencies](../dependencies.md) for SDK preparation/discovery.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS zstd)
target_link_libraries(app PRIVATE raftpp::zstd)
```

## Public API

The declarations below use namespace `raftpp`. Objects are default-constructible with no separate settings or asynchronous request state.

```cpp
class zstd_codec final : public compression_codec {
public:
    error_code compress(view<const byte> input, buffer& output) const noexcept override;
    error_code decompress(view<const byte> input, buffer& output,
                          size_t max_size) const noexcept override;
};
```

| Argument/result | Contract |
| --- | --- |
| `input` | Read-only byte view valid throughout the call; input is not modified. |
| `output` | Move-only owned buffer replaced by the complete result on success, rather than appended to. |
| `max_size` | Maximum decoded byte count, not a bound on encoded input or SDK internal memory. |
| Empty `error_code` | One frame/stream operation completed. No callback. |

Uses native `ZSTD_compress` at level 3. When the CONFIG package provides both shared and static targets, the module selects shared. The API exposes no compression-level, dictionary, or worker-count setting.

## Ownership and Input Boundaries

`compress` accepts empty original input and produces a valid empty-payload frame. `decompress` accepts exactly one complete frame/stream, rejecting truncation, invalid format, trailing bytes, and concatenated frames. Empty encoded input returns `corrupt`. A valid empty-payload frame can be decoded with `max_size=0`.

Both success and failure preserve output's allocator. Failure also preserves output contents and size. This includes input aliasing output's original storage: results are completed in a temporary buffer before replacement. Do not reuse views into old output storage after success.

Data storage uses output's allocator; native SDK contexts/internal allocations do not all use that allocator. Each call has independent native state. For concurrent calls, the caller must ensure safe input/output and allocator-resource access.

## Errors

| Error | Meaning |
| --- | --- |
| `errc::corrupt` | Invalid/truncated input, trailing bytes, or another single-frame contract violation during decoding. |
| `errc::too_big` | Decoded size exceeds `max_size`, or a native/buffer length limit is exceeded. |
| `errc::out_of_memory` | Explicitly reported allocation failure, such as a working buffer or native-context creation. |
| `errc::io_error` | Other native compression/initialization failure. |

These `noexcept` operations report results through error codes. They do not trust frame size fields to allocate an unbounded result; actual decoded bytes are bounded too. The returned code is authoritative; this codec API has no `error_details()` method.

## Example

```cpp
#include <raftpp/zstd/zstd.hpp>

raftpp::error_code round_trip(raftpp::view<const raftpp::byte> input,
                             raftpp::buffer& decoded)
{
    raftpp::zstd_codec codec;
    raftpp::buffer encoded;
    if (auto error = codec.compress(input, encoded))
        return error;
    return codec.decompress(encoded.bytes(), decoded, 8 * 1024 * 1024);
}
```

Retain input for the duration of the call. The example's decoded limit is 8 MiB; larger payloads return `too_big`. Output lifetime is independent of the codec object.

Handles Zstandard frames without a dictionary argument. It does not decode dictionary-dependent input using a separately configured dictionary context. Selecting this module does not convert Raft messages or UV snapshots to Zstandard.

Selecting a codec does not automatically alter storage/network formats or snapshot compression. See [streams/executors/codecs](../streams.md) and [buffers](../storage.md) for common ownership and abstract interfaces.
