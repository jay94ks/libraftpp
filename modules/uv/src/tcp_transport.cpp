// Port of cowsql/raft uv_tcp*.c, uv_ip.c, uv_send.c and uv_recv.c.
// Original copyright and license: LICENSE.md.
#include <raftpp/uv/tcp_transport.hpp>
#include <raftpp/uv/uv_stream_transport.hpp>
#include "uv_error.hpp"
#include "byte.hpp"
#include "tracing.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <netdb.h>
#include <uv.h>

namespace raftpp::detail {
    namespace {
        struct ip_address {
            array<char, NI_MAXHOST> host{};
            array<char, NI_MAXSERV> service{};
        };
        error_code split_address(string_view input, ip_address& output) noexcept {
            if (input.find('\0') != string_view::npos)
                return errc::invalid;
            const auto colon = input.find(':');
            const auto host = input.substr(0, colon);
            auto service = colon == string_view::npos ? string_view{} : input.substr(colon + 1);
            if (service.empty())
                service = "8080";
            if (host.size() >= output.host.size() || service.size() >= output.service.size())
                return errc::name_too_long;
            output = {};
            std::copy(host.begin(), host.end(), output.host.begin());
            std::copy(service.begin(), service.end(), output.service.begin());
            return {};
        }
        error_info tcp_info(error_code code, string_view operation, server_id id,
                            string_view address, error_code cause = {}) noexcept {
            array<char, 256> resource{};
            std::snprintf(resource.data(), resource.size(), "server %llu at %.*s",
                          static_cast<unsigned long long>(id),
                          static_cast<int>(std::min(
                              address.size(), static_cast<size_t>(numeric_limits<int>::max()))),
                          address.empty() ? "" : address.data());
            return make_error_info(code, operation, resource.data(), cause);
        }
        struct address_deleter {
            void operator()(addrinfo* value) const noexcept {
                if (value)
                    uv_freeaddrinfo(value);
            }
        };
    }

    struct tcp_transport_state : enable_shared_from_this<tcp_transport_state> {
        struct peer;
        struct listener {
            borrowed<tcp_transport_state> owner;
            uv_tcp_t socket{};
            bool live = false;
        };
        struct send_operation {
            borrowed<peer> owner;
            uv_write_t request{};
            shared<const message> value;
            encoded_message encoded;
            resource_vector<uv_buf_t> buffers;
            completion callback;
            error_info diagnostic;
        };
        enum class after_close { none, next_address, retry_delay, reconnect };
        struct peer {
            peer() : writing(pending.get_allocator()) {}
            borrowed<tcp_transport_state> owner;
            server_id id;
            resource_string address;
            ip_address target;
            uv_timer_t timer{};
            uv_tcp_t socket{};
            uv_getaddrinfo_t dns{};
            uv_connect_t connect{};
            uv_write_t handshake{};
            owned<addrinfo, address_deleter> addresses;
            borrowed<addrinfo> next = nullptr;
            // These queues exchange nodes with splice, even after selection changes.
            resource_list<resource_owned<send_operation>> pending, writing;
            bool timer_live = false, socket_live = false, resolving = false;
            bool connecting = false, handshaking = false, ready = false, retired = false;
            after_close action = after_close::none;
            error_info diagnostic;
            stream_connect_callback connected;
            error_code connect_status;
            bool connect_accepted = false;
        };
        enum class receive_stage { handshake, address, preamble, header, payload };
        struct incoming {
            borrowed<tcp_transport_state> owner;
            uv_tcp_t socket{};
            bool live = false;
            receive_stage stage = receive_stage::handshake;
            buffer input{24};
            size_t used = 0;
            server_id id = 0;
            resource_string address;
            message_preamble preamble;
            message_header header;
        };
        // No native object is erased during a callback or a reentrant user call.
        // The outermost guard collects only fully drained objects.
        struct guard {
            shared<tcp_transport_state> state;
            explicit guard(tcp_transport_state& value) noexcept : state(value.shared_from_this()) {
                ++state->depth;
            }
            ~guard() {
                if (--state->depth == 0)
                    state->collect();
            }
        };
        borrowed<uv_loop_t> loop;
        tcp_transport_options options;
        shared<tcp_transport_state> self;
        server_id id = 0;
        resource_string address;
        buffer handshake;
        shared<function<void(message)>> receive;
        shared<stream_accept_callback> accept_stream;
        close_callback closed_callback;
        resource_list<resource_owned<listener>> listeners;
        resource_list<resource_owned<peer>> peers;
        resource_list<resource_owned<incoming>> connections;
        error_code error;
        error_info diagnostic;
        unsigned depth = 0;
        bool initialized = false, started = false, closing = false, closed = false;
        bool raw_mode = false;

        tcp_transport_state(uv_loop_t& value, tcp_transport_options settings)
            : loop(&value), options(std::move(settings)) {}

        error_code initialize(server_id id, string_view address) noexcept {
            auto& state = *this;
            guard scope{state};
            const auto status = [&]() -> error_code {
                if (state.closing)
                    return errc::shutdown;
                if (state.initialized)
                    return errc::busy;
                if (id == 0)
                    return errc::bad_id;
                detail::ip_address parsed;
                if (auto status = detail::split_address(address, parsed))
                    return status;
                if (address.size() > numeric_limits<size_t>::max() - 8)
                    return errc::too_big;
                const auto padded = (address.size() + 8) / 8 * 8;
                if (padded > state.options.max_handshake_address ||
                    padded > numeric_limits<size_t>::max() - 24)
                    return errc::too_big;
                try {
                    resource_string copy{address, state.address.get_allocator()};
                    buffer bytes{24 + padded};
                    detail::byte_writer writer{bytes.bytes()};
                    writer.write<uint64_t>(1);
                    writer.write<uint64_t>(id);
                    writer.write<uint64_t>(padded);
                    writer.write({reinterpret_cast<const byte*>(copy.data()), copy.size()});
                    state.address = std::move(copy);
                    state.handshake = std::move(bytes);
                    state.id = id;
                    state.initialized = true;
                    return {};
                } catch (const bad_alloc&) {
                    return errc::out_of_memory;
                } catch (const length_error&) {
                    return errc::too_big;
                } catch (...) {
                    return errc::invalid;
                }
            }();
            if (status)
                state.note(status, "tcp.init", id, address, {}, false);
            return status;
        }
        error_code set_bind(string_view address) noexcept {
            auto& state = *this;
            const auto status = [&]() -> error_code {
                if (state.closing)
                    return errc::shutdown;
                if (state.initialized)
                    return errc::busy;
                detail::ip_address parsed;
                if (auto error = detail::split_address(address, parsed))
                    return error;
                try {
                    string copy{address};
                    state.options.bind_address = std::move(copy);
                    return {};
                } catch (const bad_alloc&) {
                    return errc::out_of_memory;
                } catch (const length_error&) {
                    return errc::too_big;
                }
            }();
            if (status)
                state.note(status, "tcp.bind_address", state.id, address, {}, false);
            return status;
        }
        error_code note(error_code status, string_view operation, server_id peer_id,
                        string_view endpoint, error_code cause = {},
                        bool record_status = true) noexcept {
            diagnostic = tcp_info(status, operation, peer_id, endpoint, cause);
            if (record_status)
                error = status;
            return status;
        }
        error_code local(error_code status, string_view operation, error_code cause = {},
                         bool record_status = true) noexcept {
            return note(status, operation, id,
                        options.bind_address.empty() ? string_view{address}
                                                     : string_view{options.bind_address},
                        cause, record_status);
        }
        void connection_error(peer& value, string_view operation, int native) noexcept {
            value.diagnostic =
                tcp_info(errc::no_connection, operation, value.id, value.address, uv_cause(native));
            diagnostic = value.diagnostic;
        }
        void incoming_error(incoming& value, error_code status, string_view operation,
                            int native = 0) noexcept {
            note(status, operation, value.id,
                 value.address.empty() ? string_view{address} : string_view{value.address},
                 uv_cause(native));
        }
        template <typename T>
        uv_stream handoff(resource_list<resource_owned<T>>& owners, T& value, uv_stream_t* native) {
            auto position = std::find_if(owners.begin(), owners.end(),
                                         [&](const auto& item) { return item.get() == &value; });
            // Prepare every fallible owner/callable allocation before adoption.
            // The native object's original allocation remains stationary.
            auto holder =
                std::allocate_shared<resource_owned<T>>(resource_allocator<resource_owned<T>>{});
            stream_deleter release = [holder](borrowed<uv_stream_s>) noexcept { holder->reset(); };
            uv_stream result{*native, std::move(release)};
            *holder = std::move(*position);
            owners.erase(position);
            return result;
        }
        void accept_handoff(incoming& value) noexcept {
            bool handed_off = false;
            try {
                string endpoint{value.address};
                const auto id = value.id;
                const auto stopped = uv_read_stop(reinterpret_cast<uv_stream_t*>(&value.socket));
                if (stopped) {
                    incoming_error(value, native_error(stopped), "tcp.stream.read.stop", stopped);
                    close_incoming(value);
                    return;
                }
                auto stream =
                    handoff(connections, value, reinterpret_cast<uv_stream_t*>(&value.socket));
                value.live = false;
                handed_off = true;
                auto callback = accept_stream;
                if (callback && !closing)
                    (*callback)(id, std::move(endpoint), std::move(stream));
            } catch (const bad_alloc&) {
                if (!handed_off) {
                    incoming_error(value, errc::out_of_memory, "tcp.stream.accept.allocate");
                    close_incoming(value);
                } else {
                    local(errc::out_of_memory, "tcp.stream.accept.callback");
                    shutdown({});
                }
            } catch (...) {
                if (!handed_off) {
                    incoming_error(value, errc::invalid, "tcp.stream.accept");
                    close_incoming(value);
                } else {
                    local(errc::invalid, "tcp.stream.accept.callback");
                    shutdown({});
                }
            }
        }
        void connect_handoff(peer& value) noexcept {
            bool handed_off = false;
            try {
                auto stream = handoff(peers, value, reinterpret_cast<uv_stream_t*>(&value.socket));
                value.socket_live = false;
                value.retired = true;
                handed_off = true;
                auto callback = std::move(value.connected);
                if (callback)
                    callback({}, std::move(stream));
            } catch (const bad_alloc&) {
                if (!handed_off) {
                    value.connect_status = errc::out_of_memory;
                    retire(value);
                } else {
                    local(errc::out_of_memory, "tcp.stream.connect.callback");
                    shutdown({});
                }
            } catch (...) {
                if (!handed_off) {
                    value.connect_status = errc::invalid;
                    retire(value);
                } else {
                    local(errc::invalid, "tcp.stream.connect.callback");
                    shutdown({});
                }
            }
        }
        void collect() noexcept {
            if (raw_mode) {
                for (;;) {
                    const auto position =
                        std::find_if(peers.begin(), peers.end(), [](const auto& value) {
                            return value->retired && value->connect_accepted && value->connected &&
                                   !value->socket_live && !value->resolving && !value->connecting &&
                                   !value->handshaking;
                        });
                    if (position == peers.end())
                        break;
                    auto value = std::move(*position);
                    peers.erase(position);
                    auto callback = std::move(value->connected);
                    const auto status = value->connect_status
                                            ? value->connect_status
                                            : make_error_code(errc::no_connection);
                    diagnostic =
                        value->diagnostic.code == status
                            ? value->diagnostic
                            : tcp_info(status, "tcp.stream.connect", value->id, value->address);
                    ++depth;
                    try {
                        callback(status, {});
                    } catch (...) {
                        local(errc::invalid, "tcp.stream.connect.callback");
                        shutdown({});
                    }
                    --depth;
                }
            }
            listeners.remove_if([](const auto& value) { return !value->live; });
            connections.remove_if([](const auto& value) { return !value->live; });
            peers.remove_if([](const auto& value) {
                return value->retired && !value->timer_live && !value->socket_live &&
                       !value->resolving && !value->connecting && !value->handshaking &&
                       value->writing.empty() && value->pending.empty();
            });
            if (!closing || closed || !listeners.empty() || !connections.empty() || !peers.empty())
                return;
            closed = true;
            auto callback = std::move(closed_callback);
            self.reset();
            if (callback)
                try {
                    callback();
                } catch (...) {
                    local(errc::invalid, "tcp.close.callback");
                }
        }
        void complete(resource_owned<send_operation> operation, error_code status) noexcept {
            auto callback = std::move(operation->callback);
            const auto info =
                operation->diagnostic.code == status
                    ? operation->diagnostic
                    : tcp_info(status, "tcp.send.complete", operation->value->server_id,
                               operation->value->server_address);
            if (status)
                diagnostic = info;
            if (callback)
                try {
                    callback(status);
                } catch (...) {
                    local(errc::invalid, "tcp.send.callback");
                    shutdown({});
                }
        }
        void close_incoming(incoming& value) noexcept {
            if (!value.live || uv_is_closing(reinterpret_cast<uv_handle_t*>(&value.socket)))
                return;
            uv_read_stop(reinterpret_cast<uv_stream_t*>(&value.socket));
            uv_close(reinterpret_cast<uv_handle_t*>(&value.socket), [](uv_handle_t* handle) {
                auto& connection = *static_cast<incoming*>(handle->data);
                guard scope{*connection.owner};
                connection.live = false;
            });
        }
        void close_socket(peer& value, after_close action) noexcept {
            value.ready = false;
            if (value.retired)
                action = after_close::none;
            value.action = action;
            if (!value.socket_live)
                return;
            if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&value.socket)))
                return;
            uv_close(reinterpret_cast<uv_handle_t*>(&value.socket), [](uv_handle_t* handle) {
                auto& target = *static_cast<peer*>(handle->data);
                auto& state = *target.owner;
                guard scope{state};
                target.socket_live = false;
                const auto action = std::exchange(target.action, after_close::none);
                if (target.retired)
                    return;
                switch (action) {
                case after_close::next_address:
                    state.connect_next(target);
                    break;
                case after_close::retry_delay:
                    state.connection_failed(target);
                    break;
                case after_close::reconnect:
                    state.resolve(target);
                    break;
                case after_close::none:
                    break;
                }
            });
        }
        void retire(peer& value) noexcept {
            if (value.retired)
                return;
            value.retired = true;
            if (raw_mode && (closing || !value.connect_status))
                value.connect_status = errc::canceled;
            if (value.resolving)
                (void)uv_cancel(reinterpret_cast<uv_req_t*>(&value.dns));
            close_socket(value, after_close::none);
            if (value.timer_live && !uv_is_closing(reinterpret_cast<uv_handle_t*>(&value.timer))) {
                uv_timer_stop(&value.timer);
                uv_close(reinterpret_cast<uv_handle_t*>(&value.timer), [](uv_handle_t* handle) {
                    auto& target = *static_cast<peer*>(handle->data);
                    guard scope{*target.owner};
                    target.timer_live = false;
                });
            }
            while (!value.pending.empty()) {
                auto operation = std::move(value.pending.front());
                value.pending.pop_front();
                complete(std::move(operation), errc::canceled);
            }
        }
        void shutdown(close_callback callback) noexcept {
            if (callback && !closed_callback && !closed)
                closed_callback = std::move(callback);
            if (closing)
                return;
            closing = true;
            receive.reset();
            accept_stream.reset();
            options.tracing.log(trace_level::trace, "closing tcp transport");
            for (auto& value : listeners)
                if (value->live && !uv_is_closing(reinterpret_cast<uv_handle_t*>(&value->socket))) {
                    uv_close(reinterpret_cast<uv_handle_t*>(&value->socket),
                             [](uv_handle_t* handle) {
                                 auto& listener =
                                     *static_cast<tcp_transport_state::listener*>(handle->data);
                                 guard scope{*listener.owner};
                                 listener.live = false;
                             });
                }
            for (auto& value : connections)
                close_incoming(*value);
            for (auto& value : peers)
                retire(*value);
        }
        void retry(peer& value) noexcept {
            if (value.retired)
                return;
            const int status = uv_timer_start(
                &value.timer,
                [](uv_timer_t* timer) {
                    auto& target = *static_cast<peer*>(timer->data);
                    guard scope{*target.owner};
                    target.owner->resolve(target);
                },
                options.connect_retry, 0);
            if (status) {
                note(native_error(status), "tcp.retry.start", value.id, value.address,
                     uv_cause(status));
                retire(value);
            }
        }
        void connection_failed(peer& value) noexcept {
            value.addresses.reset();
            value.next = nullptr;
            if (value.retired)
                return;
            if (raw_mode) {
                value.connect_status = errc::no_connection;
                retire(value);
                return;
            }
            error = errc::no_connection;
            const auto info = value.diagnostic.code == error
                                  ? value.diagnostic
                                  : tcp_info(error, "tcp.connect", value.id, value.address);
            diagnostic = info;
            logf(options.tracing, trace_level::trace, source_location::current(),
                 "server %llu connection failed; %zu pending messages",
                 static_cast<unsigned long long>(value.id), value.pending.size());
            // Match C's failed-connect eviction: keep the newest three requests.
            const auto excess = value.pending.size() > 3 ? value.pending.size() - 3 : 0;
            for (size_t i = 0; i < excess && !value.pending.empty() && !value.retired; ++i) {
                auto operation = std::move(value.pending.front());
                value.pending.pop_front();
                operation->diagnostic = info;
                complete(std::move(operation), errc::no_connection);
            }
            retry(value);
        }
        void connect_next(peer& value) noexcept {
            if (value.retired)
                return;
            while (value.next && value.next->ai_family != AF_INET)
                value.next = value.next->ai_next;
            if (!value.next) {
                connection_failed(value);
                return;
            }
            int status = uv_tcp_init(loop, &value.socket);
            if (status) {
                connection_error(value, "tcp.connect.init", status);
                connection_failed(value);
                return;
            }
            value.socket_live = true;
            value.socket.data = &value;
            value.connect.data = &value;
            status = uv_tcp_connect(
                &value.connect, &value.socket, value.next->ai_addr,
                [](uv_connect_t* request, int status) {
                    auto& target = *static_cast<peer*>(request->data);
                    auto& state = *target.owner;
                    guard scope{state};
                    target.connecting = false;
                    if (target.retired)
                        return;
                    if (status) {
                        state.connection_error(target, "tcp.connect.complete", status);
                        target.next = target.next->ai_next;
                        state.close_socket(target, after_close::next_address);
                        return;
                    }
                    target.handshake.data = &target;
                    uv_buf_t bytes{reinterpret_cast<char*>(state.handshake.data()),
                                   state.handshake.size()};
                    const int submitted = uv_write(
                        &target.handshake, reinterpret_cast<uv_stream_t*>(&target.socket), &bytes,
                        1, [](uv_write_t* write, int result) {
                            auto& target = *static_cast<peer*>(write->data);
                            auto& state = *target.owner;
                            guard scope{state};
                            target.handshaking = false;
                            if (target.retired)
                                return;
                            if (result) {
                                state.connection_error(target, "tcp.handshake.complete", result);
                                state.close_socket(target, after_close::retry_delay);
                                return;
                            }
                            target.addresses.reset();
                            target.next = nullptr;
                            target.ready = true;
                            if (state.raw_mode)
                                state.connect_handoff(target);
                            else
                                state.flush(target);
                        });
                    if (submitted) {
                        state.connection_error(target, "tcp.handshake.submit", submitted);
                        state.close_socket(target, after_close::retry_delay);
                    } else
                        target.handshaking = true;
                });
            if (status) {
                connection_error(value, "tcp.connect.submit", status);
                close_socket(value, after_close::retry_delay);
            } else
                value.connecting = true;
        }
        int resolve(peer& value) noexcept {
            if (value.retired || value.resolving || value.socket_live)
                return UV_EBUSY;
            logf(options.tracing, trace_level::trace, source_location::current(),
                 "connect server %llu at %s", static_cast<unsigned long long>(value.id),
                 value.address.c_str());
            value.dns.data = &value;
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            const int status = uv_getaddrinfo(
                loop, &value.dns,
                [](uv_getaddrinfo_t* request, int status, addrinfo* result) {
                    auto& target = *static_cast<peer*>(request->data);
                    auto& state = *target.owner;
                    guard scope{state};
                    target.resolving = false;
                    target.addresses.reset(result);
                    if (target.retired)
                        return;
                    if (status || !result) {
                        state.connection_error(target, "tcp.resolve.complete", status);
                        state.connection_failed(target);
                        return;
                    }
                    target.next = result;
                    state.connect_next(target);
                },
                value.target.host.data(), value.target.service.data(), &hints);
            if (status) {
                connection_error(value, "tcp.resolve.submit", status);
                if (raw_mode) {
                    value.connect_status = errc::no_connection;
                    retire(value);
                } else
                    retry(value); // No message eviction on immediate failure.
            } else
                value.resolving = true;
            return status;
        }
        int write(send_operation& operation) noexcept {
            auto& target = *operation.owner;
            operation.request.data = &operation;
            const auto submitted = uv_write(
                &operation.request, reinterpret_cast<uv_stream_t*>(&target.socket),
                operation.buffers.data(), static_cast<unsigned>(operation.buffers.size()),
                [](uv_write_t* request, int status) {
                    auto& operation = *static_cast<send_operation*>(request->data);
                    auto& target = *operation.owner;
                    auto& state = *target.owner;
                    guard scope{state};
                    auto position =
                        std::find_if(target.writing.begin(), target.writing.end(),
                                     [&](const auto& value) { return value.get() == &operation; });
                    auto done = std::move(*position);
                    target.writing.erase(position);
                    if (status && !target.retired)
                        state.close_socket(target, after_close::reconnect);
                    const error_code result =
                        status == 0 ? error_code{}
                                    : make_error_code(status == UV_ECANCELED && target.retired
                                                          ? errc::canceled
                                                          : errc::io_error);
                    if (result) {
                        state.error = result;
                        done->diagnostic = tcp_info(result, "tcp.write.complete", target.id,
                                                    target.address, uv_cause(status));
                    }
                    state.complete(std::move(done), result);
                });
            if (submitted)
                operation.diagnostic = tcp_info(errc::io_error, "tcp.write.submit", target.id,
                                                target.address, uv_cause(submitted));
            return submitted;
        }
        void flush(peer& value) noexcept {
            while (value.ready && !value.retired && !value.pending.empty()) {
                auto position = value.pending.begin();
                value.writing.splice(value.writing.end(), value.pending, position);
                if (const int status = write(**position)) {
                    auto done = std::move(*position);
                    value.writing.erase(position);
                    error = errc::io_error;
                    complete(std::move(done), error);
                }
            }
        }
        void deliver(incoming& value) {
            message decoded;
            shared_buffer payload;
            if (value.stage == receive_stage::payload)
                payload = shared_buffer{std::move(value.input)};
            if (auto status = decode_message_payload(value.header, std::move(payload), decoded)) {
                incoming_error(value, status, "tcp.decode.payload");
                close_incoming(value);
                return;
            }
            decoded.server_id = value.id;
            decoded.server_address = value.address;
            logf(options.tracing, trace_level::trace, source_location::current(),
                 "received RPC %zu from server %llu", decoded.payload.index() + 1,
                 static_cast<unsigned long long>(value.id));
            value.stage = receive_stage::preamble;
            value.used = 0;
            value.input = buffer{16};
            value.header = {};
            auto callback = receive;
            if (callback && !closing) {
                try {
                    (*callback)(std::move(decoded));
                } catch (...) {
                    incoming_error(value, errc::invalid, "tcp.receive.callback");
                    shutdown({});
                }
            }
        }
        void advance(incoming& value) {
            switch (value.stage) {
            case receive_stage::handshake: {
                byte_reader reader{value.input.bytes()};
                uint64_t protocol, size;
                if (!reader.read(protocol) || !reader.read(value.id) || !reader.read(size) ||
                    protocol != 1 || value.id == 0 || size == 0 || size % 8 != 0) {
                    incoming_error(value, errc::malformed, "tcp.handshake.decode");
                    close_incoming(value);
                    return;
                }
                if (size > options.max_handshake_address || size > numeric_limits<size_t>::max()) {
                    incoming_error(value, errc::too_big, "tcp.handshake.size");
                    close_incoming(value);
                    return;
                }
                value.input = buffer{static_cast<size_t>(size)};
                value.stage = receive_stage::address;
                break;
            }
            case receive_stage::address: {
                const auto bytes = value.input.bytes();
                auto end = std::find(bytes.begin(), bytes.end(), byte{});
                if (end == bytes.end() ||
                    !std::all_of(end, bytes.end(), [](byte value) { return value == byte{}; })) {
                    incoming_error(value, errc::malformed, "tcp.handshake.decode");
                    close_incoming(value);
                    return;
                }
                value.address.assign(reinterpret_cast<const char*>(bytes.data()),
                                     static_cast<size_t>(end - bytes.begin()));
                if (raw_mode) {
                    accept_handoff(value);
                    return;
                }
                logf(options.tracing, trace_level::trace, source_location::current(),
                     "accepted server %llu at %s", static_cast<unsigned long long>(value.id),
                     value.address.c_str());
                value.input = buffer{16};
                value.stage = receive_stage::preamble;
                break;
            }
            case receive_stage::preamble:
                if (auto status =
                        decode_preamble(value.input.bytes(), value.preamble, options.limits)) {
                    incoming_error(value, status, "tcp.decode.preamble");
                    close_incoming(value);
                    return;
                }
                value.input = buffer{value.preamble.header_size};
                value.stage = receive_stage::header;
                break;
            case receive_stage::header:
                if (auto status = decode_message_header(value.preamble, value.input.bytes(),
                                                        value.header, options.limits)) {
                    incoming_error(value, status, "tcp.decode.header");
                    close_incoming(value);
                    return;
                }
                if (value.header.payload_size == 0) {
                    deliver(value);
                    return;
                }
                value.input = buffer{value.header.payload_size};
                value.stage = receive_stage::payload;
                break;
            case receive_stage::payload:
                deliver(value);
                return;
            }
            value.used = 0;
        }
        void accept(listener& source) noexcept {
            if (closing)
                return;
            try {
                auto connection = allocate_owned<incoming>(resource_allocator<incoming>{});
                connection->owner = this;
                auto& value = *connection;
                connections.push_back(std::move(connection));
                int status = uv_tcp_init(loop, &value.socket);
                if (status) {
                    local(native_error(status), "tcp.accept.init", uv_cause(status));
                    return;
                }
                value.live = true;
                value.socket.data = &value;
                status = uv_accept(reinterpret_cast<uv_stream_t*>(&source.socket),
                                   reinterpret_cast<uv_stream_t*>(&value.socket));
                if (status) {
                    incoming_error(value, native_error(status), "tcp.accept", status);
                    close_incoming(value);
                    return;
                }
                status = uv_read_start(
                    reinterpret_cast<uv_stream_t*>(&value.socket),
                    [](uv_handle_t* handle, size_t, uv_buf_t* output) {
                        auto& value = *static_cast<incoming*>(handle->data);
                        output->base = reinterpret_cast<char*>(value.input.data() + value.used);
                        output->len = value.input.size() - value.used;
                    },
                    [](uv_stream_t* stream, ssize_t count, const uv_buf_t*) {
                        auto& value = *static_cast<incoming*>(stream->data);
                        auto& state = *value.owner;
                        guard scope{state};
                        if (count < 0) {
                            // EOF is ordinary connection teardown. Preserve the
                            // existing last_error status while exposing its cause.
                            state.diagnostic =
                                tcp_info(errc::io_error, "tcp.read.complete", value.id,
                                         value.address.empty() ? string_view{state.address}
                                                               : string_view{value.address},
                                         uv_cause(static_cast<int>(count)));
                            state.close_incoming(value);
                            return;
                        }
                        if (count == 0)
                            return;
                        value.used += static_cast<size_t>(count);
                        if (value.used != value.input.size())
                            return;
                        try {
                            state.advance(value);
                        } catch (const bad_alloc&) {
                            state.incoming_error(value, errc::out_of_memory,
                                                 "tcp.receive.allocate");
                            state.close_incoming(value);
                        } catch (const length_error&) {
                            state.incoming_error(value, errc::too_big, "tcp.receive.allocate");
                            state.close_incoming(value);
                        } catch (...) {
                            state.incoming_error(value, errc::invalid, "tcp.receive");
                            state.close_incoming(value);
                        }
                    });
                if (status) {
                    incoming_error(value, native_error(status), "tcp.read.start", status);
                    close_incoming(value);
                }
            } catch (const bad_alloc&) {
                local(errc::out_of_memory, "tcp.accept.allocate");
            } catch (...) {
                local(errc::invalid, "tcp.accept");
            }
        }
        error_code listen(function<void(message)> callback) {
            logf(options.tracing, trace_level::trace, source_location::current(), "listen at %s",
                 options.bind_address.empty() ? address.c_str() : options.bind_address.c_str());
            ip_address bind;
            if (auto status =
                    split_address(options.bind_address.empty() ? string_view{address}
                                                               : string_view{options.bind_address},
                                  bind))
                return status;
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
            addrinfo* resolved = nullptr;
            const auto resolved_status = ::getaddrinfo(bind.host[0] ? bind.host.data() : nullptr,
                                                       bind.service.data(), &hints, &resolved);
            const auto system_error = errno;
            if (resolved_status)
                return local(errc::io_error, "tcp.listen.resolve",
                             resolver_cause(resolved_status, system_error), false);
            owned<addrinfo, decltype(&::freeaddrinfo)> addresses{resolved, &::freeaddrinfo};
            if (!raw_mode)
                receive = std::allocate_shared<function<void(message)>>(
                    resource_allocator<function<void(message)>>{}, std::move(callback));
            resource_vector<sockaddr_in> bound;
            for (auto current = resolved; current; current = current->ai_next) {
                if (current->ai_family != AF_INET || current->ai_addrlen < sizeof(sockaddr_in))
                    continue;
                const auto& endpoint = *reinterpret_cast<const sockaddr_in*>(current->ai_addr);
                if (std::any_of(bound.begin(), bound.end(), [&](const auto& value) {
                        return endpoint.sin_port == value.sin_port &&
                               endpoint.sin_addr.s_addr == value.sin_addr.s_addr;
                    }))
                    continue;
                bound.push_back(endpoint);
                auto entry = allocate_owned<listener>(resource_allocator<listener>{});
                entry->owner = this;
                auto& value = *entry;
                listeners.push_back(std::move(entry));
                int status = uv_tcp_init(loop, &value.socket);
                if (status)
                    return local(native_error(status), "tcp.listen.init", uv_cause(status), false);
                value.live = true;
                value.socket.data = &value;
                status = uv_tcp_bind(&value.socket, current->ai_addr, 0);
                if (status)
                    return local(native_error(status), "tcp.bind", uv_cause(status), false);
                status = uv_listen(reinterpret_cast<uv_stream_t*>(&value.socket), 1,
                                   [](uv_stream_t* stream, int status) {
                                       auto& value = *static_cast<listener*>(stream->data);
                                       auto& state = *value.owner;
                                       guard scope{state};
                                       if (status) {
                                           state.local(native_error(status), "tcp.listen.complete",
                                                       uv_cause(status));
                                           return;
                                       }
                                       state.accept(value);
                                   });
                if (status)
                    return local(native_error(status), "tcp.listen", uv_cause(status), false);
            }
            if (bound.empty())
                return errc::io_error;
            started = true;
            return {};
        }
        error_code submit(shared<const message> value, completion callback, error_info& immediate) {
            if (!value || value->server_id == 0)
                return errc::bad_id;
            ip_address target;
            if (auto status = split_address(value->server_address, target))
                return status;
            auto operation = allocate_owned<send_operation>(resource_allocator<send_operation>{});
            operation->value = std::move(value);
            operation->callback = std::move(callback);
            if (auto status = encode_message(*operation->value, operation->encoded, options.limits))
                return status;
            if (operation->encoded.payload.size() >= numeric_limits<unsigned>::max())
                return errc::too_big;
            operation->buffers.reserve(1 + operation->encoded.payload.size());
            auto add = [&](const shared_buffer& bytes) {
                if (!bytes.empty())
                    operation->buffers.push_back(
                        {reinterpret_cast<char*>(const_cast<byte*>(bytes.bytes().data())),
                         bytes.size()});
            };
            add(operation->encoded.header);
            for (const auto& bytes : operation->encoded.payload)
                add(bytes);
            borrowed<peer> destination = nullptr;
            for (auto& candidate : peers) {
                if (candidate->retired || candidate->id != operation->value->server_id)
                    continue;
                if (string_view{candidate->address} != operation->value->server_address)
                    retire(*candidate);
                else
                    destination = candidate.get();
            }
            if (closing)
                return errc::shutdown;
            if (!destination) {
                auto connection = allocate_owned<peer>(resource_allocator<peer>{});
                connection->owner = this;
                connection->id = operation->value->server_id;
                connection->address = operation->value->server_address;
                connection->target = target;
                destination = connection.get();
                peers.push_back(std::move(connection));
                const int status = uv_timer_init(loop, &destination->timer);
                if (status) {
                    destination->retired = true;
                    immediate = tcp_info(native_error(status), "tcp.peer.timer.init",
                                         destination->id, destination->address, uv_cause(status));
                    return native_error(status);
                }
                destination->timer_live = true;
                destination->timer.data = destination;
            }
            operation->owner = destination;
            if (destination->ready) {
                destination->writing.push_back(std::move(operation));
                auto position = std::prev(destination->writing.end());
                if (const int status = write(**position)) {
                    immediate = (*position)->diagnostic;
                    destination->writing.erase(position);
                    return errc::io_error;
                }
            } else {
                destination->pending.push_back(std::move(operation));
                if (!destination->resolving && !destination->socket_live &&
                    !uv_is_active(reinterpret_cast<uv_handle_t*>(&destination->timer)))
                    resolve(*destination);
            }
            return {};
        }
    };
}

namespace raftpp {
    tcp_transport::tcp_transport(uv_loop_s& loop, tcp_transport_options options)
        : state_(std::allocate_shared<detail::tcp_transport_state>(
              resource_allocator<detail::tcp_transport_state>{}, loop, std::move(options))) {}
    tcp_transport::~tcp_transport() {
        close();
    }
    error_code tcp_transport::init(server_id id, string_view address) noexcept {
        return state_->initialize(id, address);
    }
    error_code tcp_transport::start(function<void(message)> receive) noexcept {
        auto& state = *state_;
        detail::tcp_transport_state::guard scope{state};
        if (state.closing)
            return state.local(errc::shutdown, "tcp.start", {}, false);
        if (!state.initialized || !receive)
            return state.local(errc::invalid, "tcp.start", {}, false);
        if (state.started)
            return state.local(errc::busy, "tcp.start", {}, false);
        state.self = state_;
        state.diagnostic = {};
        error_code status;
        try {
            status = state.listen(std::move(receive));
        } catch (const bad_alloc&) {
            status = errc::out_of_memory;
        } catch (const length_error&) {
            status = errc::too_big;
        } catch (...) {
            status = errc::invalid;
        }
        if (status) {
            state.error = status;
            if (state.diagnostic.code != status)
                state.local(status, "tcp.start", {}, false);
            state.shutdown({});
        }
        return status;
    }
    error_code tcp_transport::send(shared<const message> value, completion callback) noexcept {
        auto& state = *state_;
        detail::tcp_transport_state::guard scope{state};
        const auto context = value;
        error_info immediate;
        error_code status;
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
            state.diagnostic =
                immediate.code == status
                    ? immediate
                    : detail::tcp_info(status, "tcp.send", context ? context->server_id : 0,
                                       context ? string_view{context->server_address}
                                               : string_view{});
        return status;
    }
    void tcp_transport::close(close_callback callback) noexcept {
        auto& state = *state_;
        detail::tcp_transport_state::guard scope{state};
        if (state.closed) {
            if (callback)
                try {
                    callback();
                } catch (...) {
                    state.local(errc::invalid, "tcp.close.callback");
                }
            return;
        }
        state.shutdown(std::move(callback));
    }
    error_code tcp_transport::last_error() const noexcept {
        return state_->error;
    }
    error_info tcp_transport::error_details() const noexcept {
        return state_->diagnostic;
    }
    void tcp_transport::set_tracer(tracer value) noexcept {
        state_->options.tracing = std::move(value);
    }
    error_code tcp_transport::set_bind_address(string_view address) noexcept {
        return state_->set_bind(address);
    }
    void tcp_transport::set_connect_retry_delay(unsigned msecs) noexcept {
        if (!state_->closing)
            state_->options.connect_retry = msecs;
    }
    tcp_stream_transport::tcp_stream_transport(uv_loop_s& loop, tcp_transport_options options)
        : state_(std::allocate_shared<detail::tcp_transport_state>(
              resource_allocator<detail::tcp_transport_state>{}, loop, std::move(options))) {
        state_->raw_mode = true;
    }
    tcp_stream_transport::~tcp_stream_transport() {
        close();
    }
    error_code tcp_stream_transport::init(server_id id, string_view address) noexcept {
        return state_->initialize(id, address);
    }
    error_code tcp_stream_transport::set_bind_address(string_view address) noexcept {
        return state_->set_bind(address);
    }
    error_info tcp_stream_transport::error_details() const noexcept {
        return state_->diagnostic;
    }
    error_code tcp_stream_transport::listen(stream_accept_callback callback) noexcept {
        auto& state = *state_;
        detail::tcp_transport_state::guard scope{state};
        if (state.closing)
            return state.local(errc::shutdown, "tcp.stream.listen", {}, false);
        if (!state.initialized || !callback)
            return state.local(errc::invalid, "tcp.stream.listen", {}, false);
        if (state.started)
            return state.local(errc::busy, "tcp.stream.listen", {}, false);
        state.self = state_;
        error_code status;
        try {
            state.accept_stream = std::allocate_shared<stream_accept_callback>(
                resource_allocator<stream_accept_callback>{}, std::move(callback));
            status = state.listen({});
        } catch (const bad_alloc&) {
            status = errc::out_of_memory;
        } catch (const length_error&) {
            status = errc::too_big;
        } catch (...) {
            status = errc::invalid;
        }
        if (status) {
            if (state.diagnostic.code != status)
                state.local(status, "tcp.stream.listen", {}, false);
            state.shutdown({});
        }
        return status;
    }
    error_code tcp_stream_transport::connect(server_id id, string_view address,
                                             stream_connect_callback callback) noexcept {
        auto& state = *state_;
        detail::tcp_transport_state::guard scope{state};
        if (state.closing)
            return state.note(errc::shutdown, "tcp.stream.connect", id, address, {}, false);
        if (!state.initialized || !callback)
            return state.note(errc::invalid, "tcp.stream.connect", id, address, {}, false);
        detail::ip_address target;
        if (auto error = detail::split_address(address, target))
            return state.note(error, "tcp.stream.connect.address", id, address, {}, false);
        try {
            auto request = allocate_owned<detail::tcp_transport_state::peer>(
                resource_allocator<detail::tcp_transport_state::peer>{});
            request->owner = &state;
            request->id = id;
            request->address = address;
            request->target = target;
            request->connected = std::move(callback);
            auto pointer = request.get();
            state.peers.push_back(std::move(request));
            state.self = state_;
            if (state.resolve(*pointer)) {
                pointer->connected = {};
                return errc::no_connection;
            }
            pointer->connect_accepted = true;
            return {};
        } catch (const bad_alloc&) {
            return state.note(errc::out_of_memory, "tcp.stream.connect.allocate", id, address, {},
                              false);
        } catch (const length_error&) {
            return state.note(errc::too_big, "tcp.stream.connect.allocate", id, address, {}, false);
        } catch (...) {
            return state.note(errc::invalid, "tcp.stream.connect", id, address, {}, false);
        }
    }
    void tcp_stream_transport::close(close_callback callback) noexcept {
        auto& state = *state_;
        detail::tcp_transport_state::guard scope{state};
        if (state.closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
                state.local(errc::invalid, "tcp.stream.close.callback");
            }
            return;
        }
        state.shutdown(std::move(callback));
    }

}
