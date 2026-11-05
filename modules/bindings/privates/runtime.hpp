#ifndef RAFTPP_BINDINGS_PRIVATE_RUNTIME_HPP
#define RAFTPP_BINDINGS_PRIVATE_RUNTIME_HPP
#include <raftpp/bindings/runtime.hpp>
#include <raftpp/bindings/bindings.hpp>
#include <raftpp/encoding.hpp>
#include <raftpp/transport.hpp>
#include <list>
#include <cstring>

namespace raftpp::bindings::detail {
    class packet_transport final : public transport {
    public:
        struct packet {
            uint64_t id;
            server_id peer;
            string address;
            buffer bytes;
            completion done;
            bool dispatched = false;
        };
        std::list<packet> pending;
        function<void(message)> receiver;
        close_callback on_close;
        uint64_t serial = 1;
        bool closing = false, close_sent = false, abandoned = false;
        error_code init(server_id, string_view) override {
            return {};
        }
        error_code start(function<void(message)> value) override {
            if (closing || receiver)
                return errc::busy;
            receiver = std::move(value);
            return {};
        }
        error_code send(shared<const message> value, completion callback) override {
            if (closing)
                return errc::shutdown;
            if (serial == UINT64_MAX)
                return errc::too_big;
            encoded_message encoded;
            if (auto status = encode_message(*value, encoded))
                return status;
            size_t size = encoded.header.size();
            for (const auto& part : encoded.payload) {
                if (part.size() > SIZE_MAX - size)
                    return errc::too_big;
                size += part.size();
            }
            buffer data(size);
            auto cursor = data.data();
            auto copy = [&](view<const byte> bytes) {
                if (!bytes.empty())
                    std::memcpy(cursor, bytes.data(), bytes.size());
                cursor += bytes.size();
            };
            copy(encoded.header.bytes());
            for (const auto& part : encoded.payload)
                copy(part.bytes());
            pending.push_back({serial++, value->server_id, value->server_address, std::move(data),
                               std::move(callback)});
            return {};
        }
        void close(close_callback callback) noexcept override {
            closing = true;
            receiver = {};
            auto work = std::move(pending);
            for (auto& packet : work) {
                try {
                    if (packet.done)
                        packet.done(errc::canceled);
                } catch (...) {
                }
            }
            on_close = std::move(callback);
            if (abandoned)
                finish_close();
        }
        void finish_close() noexcept {
            auto callback = std::move(on_close);
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
        }
        void abandon() noexcept {
            abandoned = true;
            if (closing)
                finish_close();
        }
    };
    runtime make_runtime(string_view directory, const raftpp_binding_runtime_options&,
                         shared<packet_transport>& network);
    uint64_t features() noexcept;
}
#endif
