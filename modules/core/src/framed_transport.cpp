#include <raftpp/framed_transport.hpp>
#include <algorithm>

namespace raftpp {
    namespace detail {
        struct framed_peer {
            server_id id;
            string address;
            shared<byte_stream> stream;
            buffer input;
            size_t target = 16;
            optional<message_preamble> preamble;
            optional<message_header> header;
            bool failed = false;
            close_callback close_observer;
        };
        struct framed_transport_state : enable_shared_from_this<framed_transport_state> {
            explicit framed_transport_state(encoding_limits value) : limits(value) {}
            encoding_limits limits;
            map<server_id, shared<framed_peer>> peers;
            function<void(message)> receive;
            close_callback on_close;
            error_info error;
            size_t closing_streams = 0;
            shared<framed_transport_state> closing_owner;
            bool initialized = false, started = false, closing = false, closed = false;
            error_code fail(error_code code) noexcept {
                error = make_error_info(code, "framed transport");
                return code;
            }
            void consume(const shared<framed_peer>& peer, view<const byte> bytes) noexcept {
                try {
                    while (!closing && !peer->failed) {
                        const auto count =
                            std::min(bytes.size(), peer->target - peer->input.size());
                        const auto previous = peer->input.size();
                        peer->input.resize(previous + count);
                        if (count)
                            std::copy_n(bytes.data(), count, peer->input.data() + previous);
                        bytes = bytes.subspan(count);
                        if (peer->input.size() != peer->target)
                            return;
                        error_code code;
                        if (!peer->preamble) {
                            message_preamble value;
                            code = decode_preamble(peer->input.bytes(), value, limits);
                            if (!code) {
                                if (value.header_size > numeric_limits<size_t>::max() - 16)
                                    code = errc::too_big;
                                else {
                                    peer->preamble = value;
                                    peer->target = 16 + value.header_size;
                                }
                            }
                        } else if (!peer->header) {
                            message_header value;
                            code = decode_message_header(
                                *peer->preamble, peer->input.bytes().subspan(16), value, limits);
                            if (!code) {
                                if (value.payload_size >
                                    numeric_limits<size_t>::max() - peer->target)
                                    code = errc::too_big;
                                else {
                                    peer->target += value.payload_size;
                                    peer->header = std::move(value);
                                }
                            }
                        } else {
                            message value;
                            size_t consumed = 0;
                            code = decode_message(shared_buffer{std::move(peer->input)}, value,
                                                  consumed, limits);
                            if (!code) {
                                value.server_id = peer->id;
                                value.server_address = peer->address;
                                peer->target = 16;
                                peer->preamble.reset();
                                peer->header.reset();
                                peer->input = buffer{};
                                auto callback = receive;
                                if (callback)
                                    callback(std::move(value));
                            }
                        }
                        if (code) {
                            peer->failed = true;
                            (void)fail(code);
                            (void)peer->stream->read_stop();
                            return;
                        }
                        if (bytes.empty() && peer->input.size() != peer->target)
                            return;
                    }
                } catch (const bad_alloc&) {
                    peer->failed = true;
                    (void)fail(errc::out_of_memory);
                    (void)peer->stream->read_stop();
                } catch (const length_error&) {
                    peer->failed = true;
                    (void)fail(errc::too_big);
                    (void)peer->stream->read_stop();
                }
            }
            void finish_close() noexcept {
                if (!closing || closed || closing_streams)
                    return;
                closed = true;
                auto callback = std::move(on_close);
                auto owner = std::move(closing_owner);
                if (callback)
                    callback();
            }
        };
    }
    framed_transport::framed_transport(encoding_limits limits)
        : state_(std::make_shared<detail::framed_transport_state>(limits)) {}
    framed_transport::~framed_transport() {
        close();
    }
    error_code framed_transport::attach(server_id id, string_view address,
                                        shared<byte_stream> stream) noexcept {
        auto state = state_;
        if (state->closing)
            return errc::shutdown;
        if (state->started)
            return errc::busy;
        if (!id || address.empty() || !stream)
            return errc::invalid;
        if (state->peers.contains(id))
            return errc::duplicate_id;
        for (const auto& [other, peer] : state->peers)
            if (peer->stream == stream)
                return errc::busy;
        try {
            auto peer = std::make_shared<detail::framed_peer>();
            peer->id = id;
            peer->address = address;
            peer->stream = std::move(stream);
            weak<detail::framed_transport_state> owner = state;
            peer->close_observer = [owner] {
                if (auto current = owner.lock()) {
                    --current->closing_streams;
                    current->finish_close();
                }
            };
            state->peers.emplace(id, std::move(peer));
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }
    error_code framed_transport::init(server_id id, string_view address) noexcept {
        auto state = state_;
        if (state->closing)
            return errc::shutdown;
        if (!id || address.empty())
            return errc::invalid;
        if (state->initialized)
            return errc::busy;
        state->initialized = true;
        return {};
    }
    error_code framed_transport::start(function<void(message)> receive) noexcept {
        auto state = state_;
        if (state->closing)
            return errc::shutdown;
        if (!state->initialized || !receive)
            return errc::invalid;
        if (state->started)
            return errc::busy;
        try {
            state->receive = std::move(receive);
            state->started = true;
            for (const auto& [id, peer] : state->peers) {
                weak<detail::framed_transport_state> owner = state;
                weak<detail::framed_peer> endpoint = peer;
                auto code = peer->stream->read_start(
                    [owner, endpoint](error_code error, shared_buffer bytes) {
                        auto state = owner.lock();
                        auto peer = endpoint.lock();
                        if (!state || !peer || state->closing)
                            return;
                        if (error) {
                            peer->failed = true;
                            state->error = peer->stream->error_details();
                            if (!state->error.code)
                                (void)state->fail(error);
                        } else
                            state->consume(peer, bytes.bytes());
                    });
                if (state->closing)
                    return {};
                if (code) {
                    for (const auto& [other, active] : state->peers)
                        (void)active->stream->read_stop();
                    state->started = false;
                    state->receive = {};
                    return state->fail(code);
                }
            }
            return {};
        } catch (const bad_alloc&) {
            for (const auto& [id, peer] : state->peers)
                (void)peer->stream->read_stop();
            state->started = false;
            state->receive = {};
            return state->fail(errc::out_of_memory);
        }
    }
    error_code framed_transport::send(shared<const message> value, completion callback) noexcept {
        auto state = state_;
        if (state->closing)
            return errc::shutdown;
        if (!state->started || !value)
            return errc::invalid;
        auto found = state->peers.find(value->server_id);
        if (found == state->peers.end() || found->second->failed)
            return errc::no_connection;
        try {
            auto peer = found->second;
            encoded_message encoded;
            if (auto code = encode_message(*value, encoded, state->limits))
                return state->fail(code);
            vector<shared_buffer> buffers;
            buffers.reserve(1 + encoded.payload.size());
            buffers.push_back(std::move(encoded.header));
            for (auto& bytes : encoded.payload)
                buffers.push_back(std::move(bytes));
            auto code =
                peer->stream->write(buffers, [state, peer, value = std::move(value),
                                              callback = std::move(callback)](error_code error) {
                    if (error) {
                        state->error = peer->stream->error_details();
                        if (!state->error.code)
                            (void)state->fail(error);
                    }
                    if (callback)
                        callback(error);
                });
            if (code)
                return state->fail(code);
            return {};
        } catch (const bad_alloc&) {
            return state->fail(errc::out_of_memory);
        } catch (const length_error&) {
            return state->fail(errc::too_big);
        }
    }
    void framed_transport::close(close_callback callback) noexcept {
        auto state = state_;
        if (state->closed) {
            if (callback)
                callback();
            return;
        }
        if (callback && !state->on_close)
            state->on_close = std::move(callback);
        if (state->closing)
            return;
        state->closing = true;
        state->closing_owner = state;
        state->receive = {};
        state->closing_streams = state->peers.size();
        for (const auto& [id, peer] : state->peers) {
            // byte_stream owns its first close observer without allocation.
            auto code = peer->stream->close(std::move(peer->close_observer));
            if (code) {
                (void)state->fail(code);
                --state->closing_streams;
            }
        }
        state->finish_close();
    }
    error_info framed_transport::error_details() const noexcept {
        return state_->error;
    }
}
