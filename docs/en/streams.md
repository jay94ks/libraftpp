# Streams, Framing, Executors and Codecs

[한국어](../ko/streams.md)

The mandatory `raftpp::core` target provides `<raftpp/stream.hpp>`, `<raftpp/framed_transport.hpp>`, `<raftpp/executor.hpp>` and `<raftpp/compression.hpp>`. Optional [adapter modules](dependencies.md) implement these abstractions. Types use namespace `raftpp`.

## Byte Streams

`byte_stream` represents one connected, ordered, reliable duplex stream. `read_start(stream_read_callback, size_t buffer_size = 65536)` starts receiving owned `shared_buffer` fragments. Fragment sizes need not match write boundaries; `buffer_size` is a read-allocation hint. A nonempty callback and nonzero size are required. End of stream is reported as `errc::no_connection`. `read_stop()` pauses notifications without discarding unread native data.

`write(view<const shared_buffer>, completion = {})` writes buffers in order. The single-buffer overload has the same contract. An immediate submission error never invokes completion. Accepted writes retain data and complete exactly once; success indicates local SDK acceptance/completion, not delivery to the remote application or Raft commit. Backends may return `busy` while another write is active. Input view storage may be destroyed after submission.

`close(close_callback = {})` stops reads, drains/cancels accepted operations and then invokes close completion. Destruction initiates close; it does not synchronously wait. Keep driving the native execution domain until close completion. Streams allow one pending close observer; another returns `busy`. User callbacks must not throw. `error_details()` returns owning diagnostic information.

| Implementation | Construction and ownership | Execution domain |
| --- | --- | --- |
| `uv_stream` | Owns the initialized native handle and supplied deleter | Its libuv loop |
| `asio_stream` | Takes an already connected `asio::ip::tcp::socket` by value/move | Its socket executor, serialized on one thread or strand |
| `mvfst_stream` | `shared<quic::QuicSocketLite> connection, quic::StreamId stream`; exclusive callback ownership for an established bidirectional stream | The connection's event-base thread |
| `msquic_stream` | Shared API table, established `HQUIC` stream and `shared<void> connection_owner`; takes native stream/callback ownership after successful construction | The connection's serialized MsQuic callback domain |

The event loop must outlive native close. `open_msquic_api()` returns a shared table released using `MsQuicClose`; it throws `system_error` on SDK failure. The MsQuic connection owner must retain its connection and registration. Never access an adopted stream through competing native callbacks or close it externally. MsQuic receive buffers are copied before returning to the SDK and send buffers remain alive until `SEND_COMPLETE`. mvfst writes copy into an SDK-owned `IOBuf`; successful completion may run inline after `writeChain` accepts that copy. Closing mvfst detaches its reader and resets only that stream, retaining other streams on the shared connection. Asio consumes the socket by value, including cleanup if construction subsequently fails. Constructors may allocate and throw; validation failures do not adopt a native stream.

Adapters preserve their native execution domains; they do not automatically marshal callbacks between event loops or synchronize a Raft node. Every stream assembled into one transport must use the same serialized domain as the node's I/O callbacks. Distinct native worker connections require an application bridge before invoking the Raft core. QUIC TLS/listener/connection setup and peer identity validation belong to that connector.

## Raft Framing

`framed_transport(encoding_limits = {})` implements core `transport`. `attach(server_id, string_view address, shared<byte_stream>)` registers each authenticated peer before `start`. Peer IDs and addresses must be nonzero/nonempty. Duplicate IDs return `duplicate_id`; sharing the same stream between peers returns `busy`. The transport exclusively owns each registered stream's read and close lifecycle. Attach is transactional on allocation failure.

Call `init(local_id, local_address)` once and `start(function<void(message)>)` once. Send uses each message's destination `server_id`. Missing or failed peers return `no_connection`. Accepted send completion follows the underlying stream's contract. Incoming messages receive the registered peer's ID/address and use the unchanged Raft wire encoding. The parser accepts arbitrary fragments and multiple frames, enforcing header/payload/entry limits before extending buffers. Invalid frames stop that peer's reader and set diagnostics. Close drains every registered stream before its completion.

The connector supplies established streams; this transport does not resolve addresses, establish TLS, authenticate peers or retry connections. It can be supplied as the `shared<transport>` argument of an I/O backend when their execution domains agree.

```cpp
#include <raftpp/asio/asio.hpp>
#include <raftpp/framed_transport.hpp>

// socket is connected and belongs to the node's serialized execution domain.
auto stream = std::make_shared<raftpp::asio_stream>(std::move(socket));
auto network = std::make_shared<raftpp::framed_transport>();
auto error = network->attach(2, "peer-2", stream);
// Check error, then let the I/O backend own network's init/start/close lifecycle.
```

## Callback Executors

`executor::post(function<void()>) noexcept` schedules one later invocation. Empty callbacks return `invalid`; failure returns an error without invoking the callback. User callbacks must not throw. A successful post transfers the callable to the native queue. Keep running that queue until accepted work drains; no coroutine or future API is provided.

`asio_executor(asio::any_io_executor)` uses native `asio::post`. `asyncpp_executor(shared<asyncpp::dispatcher>)` uses dispatcher `push` and retains the concrete dispatcher through pending callbacks. Construct its shared owner from the concrete dispatcher type, because the upstream base does not have a public virtual destructor. A null dispatcher throws `invalid_argument`. Each upstream executor retains its own thread/queue contract.

## Compression

`compression_codec` provides synchronous `compress(view<const byte>, buffer&) const noexcept` and `decompress(view<const byte>, buffer&, size_t max_size) const noexcept`. `lz4_codec`, `zstd_codec` and `zlib_codec` implement it using LZ4 frames, Zstandard frames and zlib-wrapped DEFLATE respectively.

Each decoder accepts exactly one complete frame, rejects truncation/trailing bytes and never expands beyond `max_size`. Excess expansion returns `too_big`; invalid data returns `corrupt`; allocation failure returns `out_of_memory`. Output and its allocator are preserved on failure, including when input aliases output. Empty input can be compressed; decoding an empty encoded input fails. A valid empty frame can be decoded with `max_size = 0`. Codecs are stateless and create an independent native context for each call.

```cpp
#include <raftpp/zstd/zstd.hpp>

raftpp::zstd_codec codec;
raftpp::buffer encoded, decoded;
auto error = codec.compress(data, encoded);
if (!error) error = codec.decompress(encoded.bytes(), decoded, 8 * 1024 * 1024);
```

Selecting a codec does not alter existing disk/network formats or enable snapshot compression. UV's existing LZ4 snapshot decoder retains its compatibility contract independently of these frame APIs.
