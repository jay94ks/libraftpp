#include <raftpp/framed_transport.hpp>
#include <algorithm>
#include <iostream>

namespace {
    using namespace raftpp;
    class controlled_stream final : public byte_stream {
    public:
        stream_read_callback reader;
        completion writer;
        close_callback closed;
        vector<shared_buffer> buffers;
        bool stopping = false;
        error_code read_start(stream_read_callback callback, size_t) noexcept override {
            reader = std::move(callback);
            return {};
        }
        error_code read_stop() noexcept override {
            reader = {};
            return {};
        }
        error_code write(view<const shared_buffer> bytes, completion callback) noexcept override {
            if (writer)
                return errc::busy;
            try {
                buffers.assign(bytes.begin(), bytes.end());
                writer = std::move(callback);
                return {};
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            }
        }
        error_code close(close_callback callback) noexcept override {
            stopping = true;
            reader = {};
            closed = std::move(callback);
            if (!writer)
                drain();
            return {};
        }
        void drain() {
            auto done = std::move(writer);
            buffers.clear();
            if (done)
                done(stopping ? make_error_code(errc::canceled) : error_code{});
            auto callback = std::move(closed);
            if (callback)
                callback();
        }
        void feed(view<const byte> bytes) {
            if (reader) {
                auto callback = reader;
                callback({}, shared_buffer::copy(bytes));
            }
        }
    };
    buffer encode(const message& value) {
        encoded_message encoded;
        if (encode_message(value, encoded))
            throw std::runtime_error("encode failed");
        buffer bytes = buffer::copy(encoded.header.bytes());
        for (const auto& payload : encoded.payload) {
            auto previous = bytes.size();
            bytes.resize(previous + payload.size());
            if (payload.size())
                std::copy_n(payload.bytes().data(), payload.size(), bytes.data() + previous);
        }
        return bytes;
    }
    bool check(bool value, const char* message) {
        if (!value)
            std::cerr << message << '\n';
        return value;
    }
}
int main() {
    using namespace raftpp;
    auto stream = std::make_shared<controlled_stream>();
    auto network = std::make_unique<framed_transport>();
    if (!check(!network->attach(2, "peer", stream), "attach"))
        return 1;
    if (!check(network->attach(2, "duplicate", stream) == errc::duplicate_id, "duplicate id"))
        return 1;
    if (!check(network->attach(3, "alias", stream) == errc::busy, "stream alias"))
        return 1;
    if (!check(!network->init(1, "local"), "init"))
        return 1;
    vector<message> expected;
    request_vote vote;
    vote.term = 42;
    vote.candidate_id = 2;
    expected.push_back(message{2, "peer", vote});
    request_vote_result voted;
    voted.term = 42;
    voted.vote_granted = true;
    expected.push_back(message{2, "peer", voted});
    append_entries append;
    append.term = 42;
    append.prev_log_index = 12;
    auto item = std::make_shared<entry>();
    item->term = 42;
    item->type = entry_type::command;
    const array<byte, 3> payload{byte{1}, byte{2}, byte{3}};
    item->payload = shared_buffer::copy(payload);
    append.entries.push_back(item);
    expected.push_back(message{2, "peer", append});
    append_entries_result appended;
    appended.term = 42;
    appended.last_log_index = 13;
    expected.push_back(message{2, "peer", appended});
    install_snapshot snapshot;
    snapshot.term = 42;
    snapshot.last_index = 12;
    snapshot.last_term = 41;
    if (snapshot.configuration.add(1, "local", role::voter))
        return 1;
    snapshot.configuration_index = 1;
    snapshot.data = shared_buffer::copy(payload);
    expected.push_back(message{2, "peer", snapshot});
    timeout_now timeout;
    timeout.term = 42;
    timeout.last_log_index = 13;
    expected.push_back(message{2, "peer", timeout});
    size_t received = 0;
    bool mismatch = false;
    if (network->start([&](message value) {
            if (received >= expected.size() || value.server_id != 2 ||
                value.server_address != "peer") {
                mismatch = true;
                return;
            }
            auto actual = encode(value);
            auto wanted = encode(expected[received]);
            if (actual.size() != wanted.size() ||
                !std::equal(actual.bytes().begin(), actual.bytes().end(), wanted.bytes().begin()))
                mismatch = true;
            ++received;
        }))
        return 1;
    // Exercise each RPC with a split at every byte, then coalesced frames.
    for (const auto& value : expected) {
        auto bytes = encode(value);
        for (auto byte : bytes.bytes())
            stream->feed(view<const raftpp::byte>{&byte, 1});
    }
    if (!check(received == expected.size() && !mismatch, "fragmented RPCs"))
        return 1;
    received = 0;
    buffer all;
    for (const auto& value : expected) {
        auto bytes = encode(value);
        auto previous = all.size();
        all.resize(previous + bytes.size());
        std::copy_n(bytes.data(), bytes.size(), all.data() + previous);
    }
    stream->feed(all.bytes());
    if (!check(received == expected.size() && !mismatch, "coalesced RPCs"))
        return 1;
    auto request = std::make_shared<message>(expected.front());
    weak<message> lifetime = request;
    int complete = 0, closed = 0;
    if (network->send(request, [&](error_code error) {
            if (error == errc::canceled)
                ++complete;
        }))
        return 1;
    request.reset();
    network->close([&] {
        if (complete == 1)
            ++closed;
    });
    network.reset();
    if (!check(!lifetime.expired() && closed == 0, "retain accepted send"))
        return 1;
    stream->drain();
    if (!check(lifetime.expired() && complete == 1 && closed == 1, "drain before close"))
        return 1;
    auto corrupt_stream = std::make_shared<controlled_stream>();
    framed_transport corrupt;
    if (corrupt.attach(2, "peer", corrupt_stream) || corrupt.init(1, "local") ||
        corrupt.start([&](message) { mismatch = true; }))
        return 1;
    array<byte, 16> garbage{};
    garbage[0] = byte{0xff};
    corrupt_stream->feed(garbage);
    if (!check(corrupt.error_details().code && !corrupt_stream->reader, "reject invalid preamble"))
        return 1;
    return mismatch;
}
