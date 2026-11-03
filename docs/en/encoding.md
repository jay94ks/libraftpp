# Message Encoding and Checksums

[한국어](../ko/encoding.md) · [Nodes and I/O](node.md) · [Buffers and Entries](storage.md)

Include `<raftpp.hpp>` and link `raftpp::raftpp`. All names are in namespace `raftpp`. These APIs use no libuv types and are available regardless of UV, LZ4, or fixture options. Numeric fields in network and disk formats use little endian.

## Size Limits and Errors

```cpp
struct encoding_limits {
    size_t max_header_size = 8 * 1024 * 1024;
    size_t max_payload_size = 8 * 1024 * 1024;
    size_t max_entries = 262144;
};
```

Sizes are in bytes. `max_header_size` bounds the RPC header excluding its preamble, or a batch header. `max_payload_size` bounds the entire payload, and `max_entries` bounds the batch entry count. Pass limits as the last argument of encoding and header-decoding functions. `decode_message_payload` checks the supplied payload against the already parsed header lengths.

Configured limits are inclusive. Raising them does not extend format or platform representation ranges. Batch entry counts must fit `unsigned`, individual entry lengths must fit `uint32_t`, and totals plus RPC header/snapshot lengths must fit `size_t`. Excess values return `too_big` rather than being truncated. The separate disk-record maximum of 262144 entries still applies.

Header-function success means that a length declaration was parsed; it does not establish that payload bytes exist or allocate the entire payload. Even with limits allowing very large declarations, a staged receiver must collect the required bytes. For representable declarations within the configured limits, `decode_message` returns `malformed` if input lacks header/payload bytes, preserving output and `consumed`.

Encoding and decoding functions are `noexcept` and return `error_code`. Failure preserves every output argument and `consumed`. Allocation failure returns `out_of_memory`; unrepresentable sizes or configured limit violations return `too_big`. Truncated RPCs and invalid entry types or lengths return `malformed`; unknown RPC codes return `io_error`. Truncated records return `io_error`. CRC mismatch, zero entries, or exceeding the format's maximum count returns `corrupt`. Unaligned disk lengths also return `corrupt`. An invalid disk entry type with a valid CRC returns `malformed`.

## Sending RPCs

```cpp
struct encoded_message {
    shared_buffer header;
    vector<shared_buffer> payload;
};
error_code encode_message(const message& input, encoded_message& output,
                          encoding_limits limits = {}) noexcept;
```

`header` owns the 16-byte preamble and RPC header. Preamble byte 0 is the RPC code, byte 2 is the version, and bytes 8–15 contain the RPC header length. Other preamble bytes are written as zero. RPC codes are the [message variant](node.md) index plus one. The encoder writes the existing full header layout and uses the input version in the preamble.

Send `payload` buffers consecutively. AppendEntries contains one buffer per entry; InstallSnapshot contains one snapshot-data buffer. No padding is inserted between buffers. Other messages have no payload. The vector can include zero-length entry buffers.

Header bytes are newly owned; payload bytes share storage with the input. The result remains valid after the original message is destroyed. Retain it until transport completion and do not modify shared bytes through other aliases. A null entry handle returns `malformed`. A snapshot with `needs_data == true` returns `invalid`; load stable snapshot data first.

## Receiving RPCs

```cpp
struct message_preamble {
    uint8_t type = 0, version = 0;
    size_t header_size = 0;
};
struct message_header {
    message_payload payload;
    vector<entry_header> entries;
    size_t payload_size = 0;
};
error_code decode_preamble(view<const byte> input, message_preamble& output,
                           encoding_limits limits = {}) noexcept;
error_code decode_message_header(const message_preamble& preamble,
                                 view<const byte> input, message_header& output,
                                 encoding_limits limits = {}) noexcept;
error_code decode_message_payload(const message_header& header,
                                  shared_buffer payload, message& output) noexcept;
error_code decode_message(shared_buffer input, message& output, size_t& consumed,
                          encoding_limits limits = {}) noexcept;
```

Receive in stages as follows.

1. Collect at least 16 bytes and call `decode_preamble`. Reserved preamble bytes are ignored. Zero-length headers are invalid.
2. Collect exactly `header_size` bytes and pass them to `decode_message_header`, excluding the preamble itself. Returned `entries` contain AppendEntries metadata; the AppendEntries vector inside `payload` is still empty.
3. Collect exactly `payload_size` bytes in a shared buffer and pass it to `decode_message_payload`. Entries and snapshot data become slices of this buffer.

`decode_message` is a convenience function for input containing the preamble, header, and payload. On success, `consumed` is the entire length of that one message. Subsequent messages in the input are not consumed. Short input fails, so a backend using staged APIs must collect the required bytes first.

A caller-built `message_header` passed to the payload function must also be consistent. For AppendEntries, the sum of `entries` lengths must equal `payload_size` and the actual buffer length; the entry vector inside `payload` must be empty. Other RPCs must have no `entries` metadata. InstallSnapshot must have `needs_data` set to `false`; other non-data RPCs must have an empty payload. Inconsistencies return `malformed`, unrepresentable entry lengths return `too_big`, and existing output is preserved. Zero-length entries do not advance the byte position of the next entry.

Decoded entries and snapshots share ownership of the complete input allocation. Retained entries remain valid after the original buffer or header object is destroyed. Results have `server_id` 0 and an empty `server_address`. These fields are not on the wire; the backend sets the sender ID and address from connection information.

Decoding supports legacy version-0 length inference, omitted capability fields in older vote and replication responses, and reserved trailing bytes. Legacy 32-byte RequestVote headers are decoded without flags. Unversioned vote requests and responses infer version 1 or 2 from header length. Responses without capability fields have zero `features` and `capacity`; older votes have zero `pre_vote`. TimeoutNow decodes with version 0.

## Batches and Disk Records

```cpp
struct entry_header {
    raftpp::term term = 0;
    entry_type type = entry_type::command;
    size_t size = 0;
};
error_code encode_batch_header(view<const entry_header> entries, buffer& output,
                               encoding_limits limits = {}) noexcept;
error_code decode_batch_header(view<const byte> input, vector<entry_header>& output,
                               size_t& consumed, encoding_limits limits = {}) noexcept;
error_code encode_record(view<const entry> entries, buffer& output,
                         encoding_limits limits = {}) noexcept;
error_code decode_record(shared_buffer input, vector<entry>& output,
                         size_t& consumed, encoding_limits limits = {}) noexcept;
```

A batch header contains an 8-byte entry count and a 16-byte header per entry. Entry headers contain an 8-byte term, 1-byte type, 3 reserved bytes, and a 4-byte payload length. Lengths must fit `uint32_t`. Types are command, barrier, and change. A batch header itself permits zero entries. `decode_batch_header` consumes only the first header and does not read actual payload bytes.

A disk record contains a 4-byte header CRC32, 4-byte data CRC32, batch header, and entry data, in that order. Both CRCs start at zero. Records require at least one and at most 262144 entries. Each disk payload length must be a multiple of eight. Encoding an empty batch or an unaligned payload returns `invalid`; exceeding the entry-count limit returns `too_big`. Decoding a zero count or a count above 262144 returns `corrupt`. Even within `encoding_limits`, a complete header and payload are required; truncation returns `io_error`. It does not automatically pad lengths or alter command bytes. Network encoding has no such alignment restriction.

`disk_format_version` is 1. Record functions exclude the segment's initial 8-byte format word, I/O block-alignment padding, and file-size policies; the surrounding store manages them. `decode_record` consumes exactly one CRC-verified record. Its entries share ownership of the input storage.

## Persistent Metadata Formats

```cpp
struct persistent_metadata {
    uint64_t version = 0;
    raftpp::term term = 0;
    server_id voted_for = 0;
};
error_code encode_metadata(const persistent_metadata& input, buffer& output) noexcept;
error_code decode_metadata(view<const byte> input, persistent_metadata& output) noexcept;
error_code encode_snapshot_metadata(const snapshot& input, buffer& output) noexcept;
error_code decode_snapshot_metadata(view<const byte> input, snapshot& output) noexcept;
```

`persistent_metadata::version` is the slot-update generation, distinct from the disk-format version. Generations start at 1. Its 32-byte encoding consists of four 8-byte little-endian fields: format version 1, generation, current term, and voted-for server ID. Generation 0 returns `corrupt`; short input returns `io_error`; excess length returns `corrupt`; an unsupported format version returns `malformed`. The encoder also rejects generation 0 with `corrupt`. A new store's generation 0 represents absent files and is not written to a file.

Snapshot metadata contains an 8-byte format version, 8-byte CRC slot, 8-byte configuration log index, 8-byte configuration length, and existing configuration encoding. CRC32 covers the configuration log index, length, and configuration bytes; only the CRC slot's low 32 bits are used. Configuration length must be nonzero and at most 1 MiB. Exceeding the encoding limit returns `too_big`. Zero or excessive decoded configuration length or CRC mismatch returns `corrupt`. Short input returns `io_error`. Configuration decoding errors are returned unchanged. Additional bytes after the configuration are ignored, matching existing file-reading behavior.

`encode_snapshot_metadata` uses only the input's `configuration` and `configuration_index`. Snapshot term and index belong in the filename; data buffers belong in a separate file. `decode_snapshot_metadata` likewise updates only `configuration` and `configuration_index`, preserving term, index, and data buffers. Every function preserves its output arguments on failure.

## CRC32, SHA-1, and Digest

```cpp
uint32_t crc32(view<const byte> data, uint32_t initial = 0) noexcept;

class sha1 {
public:
    sha1() noexcept = default;
    void reset() noexcept;
    void update(view<const byte> data) noexcept;
    array<byte, 20> digest() const noexcept;
};
uint64_t digest(string_view text, uint64_t n) noexcept;
```

`crc32` uses polynomial `0x04c11db7`, without reflection or final XOR. Empty input returns `initial`. Pass a previous result as the next call's `initial` to checksum consecutive buffers.

`sha1` is a value type computing incremental SHA-1 without dynamic allocation. `update` does not retain input. `digest()` returns 20 bytes without changing state, allowing repeated queries or subsequent updates. `reset` restores the empty initial state.

The free function `digest(text, n)` hashes the text bytes followed by n as an 8-byte little-endian integer, then interprets the final eight SHA-1 bytes as a little-endian integer. Ordinary C-string inputs match the original `raft_digest`. The entire `string_view` length is used, including embedded NUL bytes.

```cpp
#include <raftpp.hpp>

raftpp::uint64_t server_digest()
{
    return raftpp::digest("node-1", 42);
}

raftpp::error_code make_heartbeat(raftpp::encoded_message& output)
{
    raftpp::message message;
    raftpp::append_entries heartbeat;
    heartbeat.term = 3;
    heartbeat.prev_log_index = 10;
    heartbeat.prev_log_term = 2;
    heartbeat.leader_commit = 10;
    message.payload = heartbeat;
    return raftpp::encode_message(message, output);
}
```
