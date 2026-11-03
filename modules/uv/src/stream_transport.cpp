// C++ port of cowsql/raft uv_send.c and uv_recv.c; see LICENSE.md.
#include <raftpp/uv/stream_transport.hpp>
#include <raftpp/memory.hpp>
#include "tracing.hpp"
#include "uv_error.hpp"
#include <algorithm>
#include <cstdio>

namespace raftpp::detail {
    struct stream_transport_state : enable_shared_from_this<stream_transport_state> {
        struct peer;
        struct send_operation {
            borrowed<peer> target = nullptr;
            shared<const message> value;
            encoded_message encoded;
            resource_vector<shared_buffer> buffers;
            completion callback;
            void operator()(error_code status) noexcept;
        };
        struct peer {
            struct connect_callback {
                borrowed<peer> target;
                void operator()(error_code, uv_stream) noexcept;
            } connected{this};
            struct close_callback {
                borrowed<peer> target;
                void operator()() noexcept;
            } stream_closed{this};
            borrowed<stream_transport_state> owner;
            server_id id;
            resource_string address;
            uv_timer_t timer{};
            uv_stream stream;
            resource_list<resource_owned<send_operation>> pending;
            resource_list<resource_owned<send_operation>> writing{pending.get_allocator()};
            bool timer_live = false, connecting = false, stream_closing = false, retired = false;
        };
        enum class stage { preamble, header, payload };
        struct incoming {
            struct read_callback {
                borrowed<incoming> connection;
                void operator()(error_code, shared_buffer) noexcept;
            } read{this};
            struct close_callback {
                borrowed<incoming> connection;
                void operator()() noexcept;
            } closed{this};
            borrowed<stream_transport_state> owner;
            server_id id;
            string address;
            uv_stream stream;
            buffer input{16};
            size_t used = 0;
            stage current = stage::preamble;
            message_preamble preamble;
            message_header header;
            bool closing = false, live = true;
        };
        struct accept_callback {
            borrowed<stream_transport_state> owner;
            void operator()(server_id, string, uv_stream) noexcept;
        } accept{this};
        struct connector_close_callback {
            borrowed<stream_transport_state> owner;
            void operator()() noexcept;
        } connector_closed{this};
        struct orphan_close_callback {
            borrowed<stream_transport_state> owner;
            void operator()() noexcept;
        } orphan_closed{this};
        struct guard {
            shared<stream_transport_state> owner;
            explicit guard(stream_transport_state& state) noexcept
                : owner(state.shared_from_this()) {
                ++owner->depth;
            }
            ~guard() {
                if (--owner->depth == 0)
                    owner->collect();
            }
        };
        borrowed<uv_loop_t> loop;
        shared<uv_stream_transport> connector;
        stream_transport_options options;
        shared<stream_transport_state> self;
        shared<function<void(message)>> receive;
        resource_list<resource_owned<peer>> peers;
        resource_list<resource_owned<incoming>> connections;
        close_callback closed_callback;
        error_info diagnostic;
        unsigned depth = 0;
        size_t orphan_streams = 0;
        bool initialized = false, started = false, closing = false, closed = false;
        bool connector_used = false, connector_done = false;

        stream_transport_state(uv_loop_t& loop, shared<uv_stream_transport> connector,
                               stream_transport_options options)
            : loop(&loop), connector(std::move(connector)), options(std::move(options)) {}
        error_code note(error_code code, string_view operation, server_id id = 0,
                        string_view address = {}, error_info info = {}) noexcept {
            array<char, 256> context{};
            std::snprintf(context.data(), context.size(), "server %llu at %.*s",
                          static_cast<unsigned long long>(id),
                          static_cast<int>(std::min(
                              address.size(), static_cast<size_t>(numeric_limits<int>::max()))),
                          address.empty() ? "" : address.data());
            diagnostic = make_error_info(code, operation, context.data(), info.cause);
            return code;
        }
        void collect() noexcept {
            peers.remove_if([](const auto& value) {
                return value->retired && !value->timer_live && !value->connecting &&
                       !value->stream_closing && value->writing.empty() && value->pending.empty();
            });
            connections.remove_if([](const auto& value) { return !value->live; });
            if (!closing || closed || !connector_done || orphan_streams || !peers.empty() ||
                !connections.empty())
                return;
            closed = true;
            auto keep = self;
            auto callback = std::move(closed_callback);
            self.reset();
            try {
                if (callback)
                    callback();
            } catch (...) {
                note(errc::invalid, "stream_transport.close.callback");
            }
        }
        void complete(resource_owned<send_operation> operation, error_code status,
                      error_info info = {}) noexcept {
            if (status)
                note(status, "stream_transport.send.complete", operation->value->server_id,
                     operation->value->server_address, info);
            try {
                if (operation->callback)
                    operation->callback(status);
            } catch (...) {
                note(errc::invalid, "stream_transport.send.callback");
                shutdown({});
            }
        }
        void disconnect(peer& value) noexcept {
            if (value.stream_closing || !value.stream.native_handle())
                return;
            value.stream_closing = true;
            // The first observer is stored without allocation; std::ref also
            // fits the standard function without allocating its target.
            (void)value.stream.close(std::ref(value.stream_closed));
        }
        void retire(peer& value) noexcept {
            if (value.retired)
                return;
            value.retired = true;
            if (value.timer_live && !uv_is_closing(reinterpret_cast<uv_handle_t*>(&value.timer))) {
                uv_timer_stop(&value.timer);
                uv_close(reinterpret_cast<uv_handle_t*>(&value.timer), [](uv_handle_t* handle) {
                    auto& value = *static_cast<peer*>(handle->data);
                    guard scope{*value.owner};
                    value.timer_live = false;
                });
            }
            disconnect(value);
            while (!value.pending.empty()) {
                auto operation = std::move(value.pending.front());
                value.pending.pop_front();
                complete(std::move(operation), errc::canceled);
            }
        }
        void close_incoming(incoming& value) noexcept {
            if (value.closing)
                return;
            value.closing = true;
            (void)value.stream.close(std::ref(value.closed));
        }
        void close_orphan(uv_stream& value) noexcept {
            ++orphan_streams;
            (void)value.close(std::ref(orphan_closed));
        }
        void shutdown(close_callback callback) noexcept {
            if (callback && !closed_callback && !closed)
                closed_callback = std::move(callback);
            if (closing)
                return;
            closing = true;
            started = false;
            receive.reset();
            self = shared_from_this();
            for (auto& value : peers)
                retire(*value);
            for (auto& value : connections)
                close_incoming(*value);
            if (connector_used)
                connector->close(std::ref(connector_closed));
            else
                connector_done = true;
        }
        void retry(peer& value) noexcept {
            if (value.retired)
                return;
            const auto status = uv_timer_start(
                &value.timer,
                [](uv_timer_t* timer) {
                    auto& value = *static_cast<peer*>(timer->data);
                    guard scope{*value.owner};
                    value.owner->connect(value);
                },
                options.connect_retry, 0);
            if (status) {
                note(native_error(status), "stream_transport.retry", value.id, value.address,
                     make_error_info(native_error(status), {}, {}, uv_cause(status)));
                retire(value);
            }
        }
        void connect(peer& value) noexcept {
            if (value.retired || value.connecting || value.stream_closing || value.stream)
                return;
            value.connecting = true;
            error_code status;
            error_info info;
            try {
                status = connector->connect(value.id, value.address, std::ref(value.connected));
                if (status)
                    info = connector->error_details();
            } catch (const bad_alloc&) {
                status = errc::out_of_memory;
            } catch (...) {
                status = errc::io_error;
            }
            if (!status)
                return;
            value.connecting = false;
            note(status, "stream_transport.connect.submit", value.id, value.address, info);
            // C does not evict messages after an immediate submission error.
            retry(value);
        }
        error_code write(peer& value, send_operation& operation) noexcept {
            return value.stream.write(operation.buffers, std::ref(operation));
        }
        void flush(peer& value) noexcept {
            while (!value.retired && value.stream && !value.stream_closing &&
                   !value.pending.empty()) {
                const auto position = value.pending.begin();
                value.writing.splice(value.writing.end(), value.pending, position);
                if (const auto status = write(value, **position)) {
                    const auto info = value.stream.error_details();
                    auto operation = std::move(*position);
                    value.writing.erase(position);
                    complete(std::move(operation), errc::io_error, info);
                }
            }
        }
        error_code submit(shared<const message> message, completion callback,
                          error_info& immediate) {
            if (!message || !message->server_id)
                return errc::bad_id;
            auto operation = allocate_owned<send_operation>(resource_allocator<send_operation>{});
            operation->value = std::move(message);
            operation->callback = std::move(callback);
            if (auto status = encode_message(*operation->value, operation->encoded, options.limits))
                return status;
            operation->buffers.reserve(operation->encoded.payload.size() + 1);
            operation->buffers.push_back(operation->encoded.header);
            for (const auto& bytes : operation->encoded.payload)
                operation->buffers.push_back(bytes);
            borrowed<peer> target = nullptr;
            for (auto& item : peers) {
                if (item->retired || item->id != operation->value->server_id)
                    continue;
                if (string_view{item->address} == operation->value->server_address)
                    target = item.get();
                else
                    retire(*item);
            }
            if (closing)
                return errc::shutdown;
            if (!target) {
                auto connection = allocate_owned<peer>(resource_allocator<peer>{});
                connection->owner = this;
                connection->id = operation->value->server_id;
                connection->address = operation->value->server_address;
                target = connection.get();
                peers.push_back(std::move(connection));
                const auto status = uv_timer_init(loop, &target->timer);
                if (status) {
                    target->retired = true;
                    immediate = make_error_info(native_error(status), "stream_transport.timer.init",
                                                {}, uv_cause(status));
                    return native_error(status);
                }
                target->timer_live = true;
                target->timer.data = target;
            }
            operation->target = target;
            if (target->stream && !target->stream_closing) {
                target->writing.push_back(std::move(operation));
                const auto position = std::prev(target->writing.end());
                if (auto status = write(*target, **position)) {
                    note(errc::io_error, "stream_transport.write.submit", target->id,
                         target->address, target->stream.error_details());
                    immediate = diagnostic;
                    target->writing.erase(position);
                    return errc::io_error;
                }
            } else {
                target->pending.push_back(std::move(operation));
                if (!target->connecting && !target->stream_closing &&
                    !uv_is_active(reinterpret_cast<uv_handle_t*>(&target->timer)))
                    connect(*target);
            }
            return {};
        }
        void deliver(incoming& value) {
            message result;
            shared_buffer payload;
            if (value.current == stage::payload)
                payload = shared_buffer{std::move(value.input)};
            if (const auto status =
                    decode_message_payload(value.header, std::move(payload), result)) {
                note(status, "stream_transport.decode.payload", value.id, value.address);
                close_incoming(value);
                return;
            }
            result.server_id = value.id;
            result.server_address = value.address;
            value.input = buffer{16};
            value.used = 0;
            value.current = stage::preamble;
            value.header = {};
            auto callback = receive;
            if (!closing && callback) {
                logf(options.tracing, trace_level::trace, source_location::current(),
                     "received RPC %zu from server %llu", result.payload.index() + 1,
                     static_cast<unsigned long long>(value.id));
                try {
                    (*callback)(std::move(result));
                } catch (...) {
                    note(errc::invalid, "stream_transport.receive.callback", value.id,
                         value.address);
                    shutdown({});
                }
            }
        }
        void advance(incoming& value) {
            if (value.current == stage::preamble) {
                if (auto status =
                        decode_preamble(value.input.bytes(), value.preamble, options.limits)) {
                    note(status, "stream_transport.decode.preamble", value.id, value.address);
                    close_incoming(value);
                    return;
                }
                value.input = buffer{value.preamble.header_size};
                value.current = stage::header;
            } else if (value.current == stage::header) {
                if (auto status = decode_message_header(value.preamble, value.input.bytes(),
                                                        value.header, options.limits)) {
                    note(status, "stream_transport.decode.header", value.id, value.address);
                    close_incoming(value);
                    return;
                }
                if (!value.header.payload_size) {
                    deliver(value);
                    return;
                }
                value.input = buffer{value.header.payload_size};
                value.current = stage::payload;
            } else {
                deliver(value);
                return;
            }
            value.used = 0;
        }
    };
    void stream_transport_state::peer::connect_callback::operator()(error_code status,
                                                                    uv_stream stream) noexcept {
        auto& value = *target;
        auto& state = *value.owner;
        guard scope{state};
        value.connecting = false;
        if (!status && !stream)
            status = errc::no_connection;
        if (value.retired) {
            value.stream = std::move(stream);
            state.disconnect(value);
            return;
        }
        if (!status) {
            value.stream = std::move(stream);
            state.flush(value);
            return;
        }
        if (stream.native_handle())
            state.close_orphan(stream);
        const auto info = state.connector->error_details();
        state.note(status, "stream_transport.connect.complete", value.id, value.address, info);
        while (!value.retired && value.pending.size() > 3) {
            auto operation = std::move(value.pending.front());
            value.pending.pop_front();
            state.complete(std::move(operation), errc::no_connection, info);
        }
        state.retry(value);
    }
    void stream_transport_state::peer::close_callback::operator()() noexcept {
        auto& value = *target;
        auto& state = *value.owner;
        guard scope{state};
        value.stream_closing = false;
        value.stream = {};
        if (!value.retired)
            state.connect(value);
    }
    void stream_transport_state::send_operation::operator()(error_code status) noexcept {
        auto& value = *target;
        auto& state = *value.owner;
        guard scope{state};
        const auto info = status ? value.stream.error_details() : error_info{};
        const auto result =
            status ? make_error_code(status == errc::canceled && value.retired ? errc::canceled
                                                                               : errc::io_error)
                   : error_code{};
        const auto position = std::find_if(value.writing.begin(), value.writing.end(),
                                           [this](const auto& item) { return item.get() == this; });
        auto operation = std::move(*position);
        value.writing.erase(position);
        if (status && !value.retired)
            state.disconnect(value);
        state.complete(std::move(operation), result, info);
    }
    void stream_transport_state::incoming::read_callback::operator()(error_code status,
                                                                     shared_buffer chunk) noexcept {
        auto& value = *connection;
        auto& state = *value.owner;
        guard scope{state};
        if (value.closing || state.closing)
            return;
        if (status) {
            state.note(status, "stream_transport.read", value.id, value.address,
                       value.stream.error_details());
            state.close_incoming(value);
            return;
        }
        try {
            auto bytes = chunk.bytes();
            while (!bytes.empty() && !value.closing && !state.closing) {
                const auto size = std::min(bytes.size(), value.input.size() - value.used);
                std::copy_n(bytes.begin(), size,
                            value.input.bytes().begin() + static_cast<ptrdiff_t>(value.used));
                value.used += size;
                bytes = bytes.subspan(size);
                if (value.used == value.input.size())
                    state.advance(value);
            }
        } catch (const bad_alloc&) {
            state.note(errc::out_of_memory, "stream_transport.receive.allocate", value.id,
                       value.address);
            state.close_incoming(value);
        } catch (const length_error&) {
            state.note(errc::too_big, "stream_transport.receive.allocate", value.id, value.address);
            state.close_incoming(value);
        } catch (...) {
            state.note(errc::invalid, "stream_transport.receive", value.id, value.address);
            state.close_incoming(value);
        }
    }
    void stream_transport_state::incoming::close_callback::operator()() noexcept {
        auto& value = *connection;
        guard scope{*value.owner};
        value.live = false;
    }
    void stream_transport_state::accept_callback::operator()(server_id id, string address,
                                                             uv_stream stream) noexcept {
        auto& state = *owner;
        guard scope{state};
        if (state.closing || !id || !stream) {
            state.close_orphan(stream);
            return;
        }
        try {
            auto value = allocate_owned<incoming>(resource_allocator<incoming>{});
            value->owner = &state;
            value->id = id;
            value->address = std::move(address);
            const auto connection = value.get();
            state.connections.push_back(std::move(value));
            connection->stream = std::move(stream);
            if (auto status = connection->stream.read_start(std::ref(connection->read),
                                                            state.options.read_buffer_size)) {
                state.note(status, "stream_transport.read.start", id, connection->address,
                           connection->stream.error_details());
                state.close_incoming(*connection);
            }
        } catch (const bad_alloc&) {
            state.note(errc::out_of_memory, "stream_transport.accept.allocate", id, address);
            state.close_orphan(stream);
        } catch (...) {
            state.note(errc::invalid, "stream_transport.accept", id, address);
            state.close_orphan(stream);
        }
    }
    void stream_transport_state::connector_close_callback::operator()() noexcept {
        guard scope{*owner};
        owner->connector_done = true;
    }
    void stream_transport_state::orphan_close_callback::operator()() noexcept {
        guard scope{*owner};
        --owner->orphan_streams;
    }
}
namespace raftpp {
    stream_transport::stream_transport(uv_loop_s& loop, shared<uv_stream_transport> connector,
                                       stream_transport_options options) {
        if (!connector)
            throw invalid_argument("stream transport requires a connector");
        if (!options.read_buffer_size || options.read_buffer_size > numeric_limits<unsigned>::max())
            throw invalid_argument("invalid stream read buffer size");
        state_ = std::allocate_shared<detail::stream_transport_state>(
            resource_allocator<detail::stream_transport_state>{}, loop, std::move(connector),
            std::move(options));
    }
    stream_transport::~stream_transport() {
        close();
    }
    error_code stream_transport::init(server_id id, string_view address) noexcept {
        auto& state = *state_;
        detail::stream_transport_state::guard scope{state};
        if (state.closing)
            return state.note(errc::shutdown, "stream_transport.init");
        if (state.initialized || state.connector_used)
            return state.note(errc::busy, "stream_transport.init");
        if (!id)
            return state.note(errc::bad_id, "stream_transport.init");
        state.self = state_;
        state.connector_used = true;
        error_code status;
        error_info info;
        try {
            status = state.connector->init(id, address);
            if (status)
                info = state.connector->error_details();
        } catch (const bad_alloc&) {
            status = errc::out_of_memory;
        } catch (...) {
            status = errc::io_error;
        }
        if (status)
            return state.note(status, "stream_transport.init", id, address, info);
        state.initialized = true;
        return {};
    }
    error_code stream_transport::start(function<void(message)> receive) noexcept {
        auto& state = *state_;
        detail::stream_transport_state::guard scope{state};
        if (state.closing)
            return state.note(errc::shutdown, "stream_transport.start");
        if (!state.initialized || !receive)
            return state.note(errc::invalid, "stream_transport.start");
        if (state.started)
            return state.note(errc::busy, "stream_transport.start");
        error_code status;
        error_info info;
        try {
            state.receive = std::allocate_shared<function<void(message)>>(
                resource_allocator<function<void(message)>>{}, std::move(receive));
            state.started = true; // Plugins may invoke accept inline during listen.
            status = state.connector->listen(std::ref(state.accept));
            if (status)
                info = state.connector->error_details();
        } catch (const bad_alloc&) {
            status = errc::out_of_memory;
        } catch (...) {
            status = errc::io_error;
        }
        if (status) {
            state.note(status, "stream_transport.listen", 0, {}, info);
            state.shutdown({});
        }
        return status;
    }
    error_code stream_transport::send(shared<const message> value, completion callback) noexcept {
        auto& state = *state_;
        detail::stream_transport_state::guard scope{state};
        const auto context = value;
        error_code status;
        error_info immediate;
        if (state.closing)
            status = errc::shutdown;
        else if (!state.started)
            status = errc::invalid;
        else {
            try {
                status = state.submit(std::move(value), std::move(callback), immediate);
            } catch (const bad_alloc&) {
                status = errc::out_of_memory;
            } catch (const length_error&) {
                status = errc::too_big;
            } catch (...) {
                status = errc::invalid;
            }
        }
        if (status)
            state.note(status, "stream_transport.send", context ? context->server_id : 0,
                       context ? string_view{context->server_address} : string_view{}, immediate);
        return status;
    }
    void stream_transport::close(close_callback callback) noexcept {
        auto& state = *state_;
        detail::stream_transport_state::guard scope{state};
        if (state.closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
                state.note(errc::invalid, "stream_transport.close.callback");
            }
            return;
        }
        state.shutdown(std::move(callback));
    }
    void stream_transport::set_connect_retry_delay(unsigned msecs) noexcept {
        if (!state_->closing)
            state_->options.connect_retry = msecs;
    }
    error_info stream_transport::error_details() const noexcept {
        return state_->diagnostic;
    }
}
