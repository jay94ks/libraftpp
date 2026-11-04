# zlib Module API

[한국어](../../ko/modules/zlib.md) · [Modules](index.md)

`zlib_codec final : compression_codec` synchronously compresses/decompresses a zlib-wrapped DEFLATE stream. Select `RAFTPP_MODULE_ZLIB=ON`, link `raftpp::zlib`, and include `<raftpp/zlib/zlib.hpp>`. Depends on core and `ZLIB::ZLIB` from `find_package(ZLIB)`. UV is not required. See [dependencies](../dependencies.md) for SDK preparation/discovery.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS zlib)
target_link_libraries(app PRIVATE raftpp::zlib)
```

## Public API

The declarations below use namespace `raftpp`. Objects are default-constructible with no separate settings or asynchronous request state.

```cpp
class zlib_codec final : public compression_codec {
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

Uses native `compress2` with `Z_DEFAULT_COMPRESSION`. Compression input length must fit native `uLong`, otherwise `too_big`. Decompression feeds large input in native `uInt`-sized portions.

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
#include <raftpp/zlib/zlib.hpp>

raftpp::error_code round_trip(raftpp::view<const raftpp::byte> input,
                             raftpp::buffer& decoded)
{
    raftpp::zlib_codec codec;
    raftpp::buffer encoded;
    if (auto error = codec.compress(input, encoded))
        return error;
    return codec.decompress(encoded.bytes(), decoded, 8 * 1024 * 1024);
}
```

Retain input for the duration of the call. The example's decoded limit is 8 MiB; larger payloads return `too_big`. Output lifetime is independent of the codec object.

Uses the zlib wrapper. It does not auto-detect gzip files or raw DEFLATE; those inputs return `corrupt`. No dictionary argument is provided; dictionary-required input also returns `corrupt`. Selecting this module does not convert Raft messages or UV snapshots to DEFLATE.

Selecting a codec does not automatically alter storage/network formats or snapshot compression. See [streams/executors/codecs](../streams.md) and [buffers](../storage.md) for common ownership and abstract interfaces.
