#ifndef RAFTPP_ENCODING_HPP
#define RAFTPP_ENCODING_HPP

#include <raftpp/message.hpp>
#include <raftpp/snapshot.hpp>

namespace raftpp {

    inline constexpr uint64_t disk_format_version = 1;
    struct persistent_metadata {
        uint64_t version = 0;
        raftpp::term term = 0;
        server_id voted_for = 0;
    };
    [[nodiscard]] RAFTPP_API error_code encode_metadata(const persistent_metadata& input,
                                                        buffer& output) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_metadata(view<const byte> input,
                                                        persistent_metadata& output) noexcept;
    [[nodiscard]] RAFTPP_API error_code encode_snapshot_metadata(const snapshot& input,
                                                                 buffer& output) noexcept;
    // Only configuration and configuration_index change; index/term come from the filename.
    [[nodiscard]] RAFTPP_API error_code decode_snapshot_metadata(view<const byte> input,
                                                                 snapshot& output) noexcept;
    struct encoding_limits {
        size_t max_header_size = 8 * 1024 * 1024;
        size_t max_payload_size = 8 * 1024 * 1024;
        size_t max_entries = 262144;
    };
    struct entry_header {
        raftpp::term term = 0;
        entry_type type = entry_type::command;
        size_t size = 0;
    };
    struct encoded_message {
        shared_buffer header;          // Includes the 16-byte preamble.
        vector<shared_buffer> payload; // Consecutive wire bytes, without padding.
    };
    struct message_preamble {
        uint8_t type = 0, version = 0;
        size_t header_size = 0; // Excludes the preamble and payload.
    };
    struct message_header {
        message_payload payload;
        vector<entry_header> entries;
        size_t payload_size = 0;
    };

    // All output arguments are preserved on failure. Limit errors use too_big.
    [[nodiscard]] RAFTPP_API error_code encode_batch_header(view<const entry_header> entries,
                                                            buffer& output,
                                                            encoding_limits limits = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_batch_header(view<const byte> input,
                                                            vector<entry_header>& output,
                                                            size_t& consumed,
                                                            encoding_limits limits = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_code encode_message(const message& input,
                                                       encoded_message& output,
                                                       encoding_limits limits = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_preamble(view<const byte> input,
                                                        message_preamble& output,
                                                        encoding_limits limits = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_message_header(const message_preamble& preamble,
                                                              view<const byte> input,
                                                              message_header& output,
                                                              encoding_limits limits = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_message_payload(const message_header& header,
                                                               shared_buffer payload,
                                                               message& output) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_message(shared_buffer input, message& output,
                                                       size_t& consumed,
                                                       encoding_limits limits = {}) noexcept;

    // One segment record: two CRC32 values, batch header, aligned entry data.
    // The segment format word and direct-I/O block padding are separate.
    [[nodiscard]] RAFTPP_API error_code encode_record(view<const entry> entries, buffer& output,
                                                      encoding_limits limits = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_code decode_record(shared_buffer input, vector<entry>& output,
                                                      size_t& consumed,
                                                      encoding_limits limits = {}) noexcept;

}

#endif // RAFTPP_ENCODING_HPP
