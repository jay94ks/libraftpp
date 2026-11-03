#include <raftpp/msquic/msquic.hpp>
#include <cstring>

namespace raftpp {
    namespace {
        class native_category final : public error_category {
        public:
            const char* name() const noexcept override {
                return "msquic";
            }
            string message(int code) const override {
                return "MsQuic status " + std::to_string(code);
            }
        };
        const native_category category{};
        error_code native_error(QUIC_STATUS status) noexcept {
            return {static_cast<int>(status), category};
        }
    }
    shared<const QUIC_API_TABLE> open_msquic_api() {
        const QUIC_API_TABLE* api = nullptr;
        const auto status = MsQuicOpen2(&api);
        if (QUIC_FAILED(status))
            throw system_error(native_error(status));
        return {api, [](const QUIC_API_TABLE* value) { MsQuicClose(value); }};
    }
    namespace detail {
        struct msquic_stream_state : enable_shared_from_this<msquic_stream_state> {
            shared<const QUIC_API_TABLE> api;
            shared<void> connection;
            shared<msquic_stream_state> native_owner;
            HQUIC stream;
            stream_read_callback reader;
            close_callback on_close;
            error_info error;
            size_t pending = 0, callbacks = 0;
            bool closing = false, shutdown = false, closed = false;
            struct request {
                shared<msquic_stream_state> state;
                vector<shared_buffer> owners;
                vector<QUIC_BUFFER> native;
                completion callback;
            };
            error_code fail(QUIC_STATUS status) noexcept {
                const auto code =
                    status == QUIC_STATUS_OUT_OF_MEMORY ? errc::out_of_memory : errc::io_error;
                error = make_error_info(code, "msquic stream", {}, native_error(status));
                return code;
            }
            void finish_close() noexcept {
                if (closed || !shutdown || pending || callbacks)
                    return;
                closed = true;
                reader = {};
                if (stream) {
                    api->StreamClose(stream);
                    stream = nullptr;
                }
                connection.reset();
                auto callback = std::move(on_close);
                auto owner = std::move(native_owner);
                if (callback)
                    callback();
            }
            static QUIC_STATUS QUIC_API event(HQUIC, void* context,
                                              QUIC_STREAM_EVENT* event) noexcept {
                auto self = static_cast<msquic_stream_state*>(context)->shared_from_this();
                struct event_scope {
                    msquic_stream_state& state;
                    explicit event_scope(msquic_stream_state& value) : state(value) {
                        ++state.callbacks;
                    }
                    ~event_scope() {
                        --state.callbacks;
                        state.finish_close();
                    }
                } active{*self};
                switch (event->Type) {
                case QUIC_STREAM_EVENT_RECEIVE:
                    if (!self->reader || self->closing) {
                        event->RECEIVE.TotalBufferLength = 0;
                        return QUIC_STATUS_SUCCESS;
                    }
                    try {
                        if (event->RECEIVE.TotalBufferLength > numeric_limits<size_t>::max())
                            return QUIC_STATUS_OUT_OF_MEMORY;
                        buffer bytes{static_cast<size_t>(event->RECEIVE.TotalBufferLength)};
                        size_t offset = 0;
                        for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
                            const auto& part = event->RECEIVE.Buffers[i];
                            if (part.Length > bytes.size() - offset)
                                return QUIC_STATUS_INVALID_PARAMETER;
                            if (part.Length)
                                std::memcpy(bytes.data() + offset, part.Buffer, part.Length);
                            offset += part.Length;
                        }
                        bytes.resize(offset);
                        auto value = shared_buffer{std::move(bytes)};
                        auto callback = self->reader;
                        if (!value.empty())
                            callback({}, std::move(value));
                    } catch (const bad_alloc&) {
                        self->error = make_error_info(errc::out_of_memory, "msquic receive");
                        auto callback = std::move(self->reader);
                        if (callback)
                            callback(self->error.code, {});
                        return QUIC_STATUS_OUT_OF_MEMORY;
                    } catch (...) {
                        return QUIC_STATUS_INTERNAL_ERROR;
                    }
                    break;
                case QUIC_STREAM_EVENT_SEND_COMPLETE: {
                    owned<request> operation{
                        static_cast<request*>(event->SEND_COMPLETE.ClientContext)};
                    if (operation->callback)
                        operation->callback(event->SEND_COMPLETE.Canceled
                                                ? make_error_code(errc::canceled)
                                                : error_code{});
                    --self->pending;
                    operation.reset();
                    self->finish_close();
                    break;
                }
                case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
                case QUIC_STREAM_EVENT_PEER_SEND_ABORTED: {
                    auto callback = std::move(self->reader);
                    if (callback && !self->closing)
                        callback(errc::no_connection, {});
                    break;
                }
                case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
                    self->shutdown = true;
                    if (!self->closing) {
                        auto callback = std::move(self->reader);
                        if (callback)
                            callback(errc::no_connection, {});
                    }
                    self->finish_close();
                    break;
                default:
                    break;
                }
                return QUIC_STATUS_SUCCESS;
            }
        };
    }
    msquic_stream::msquic_stream(shared<const QUIC_API_TABLE> api, HQUIC stream,
                                 shared<void> connection_owner) {
        if (!api || !stream || !connection_owner)
            throw invalid_argument("msquic requires API, stream and connection ownership");
        auto state = std::make_shared<detail::msquic_stream_state>();
        state->api = std::move(api);
        state->connection = std::move(connection_owner);
        state->stream = stream;
        const auto status = state->api->StreamReceiveSetEnabled(stream, FALSE);
        if (QUIC_FAILED(status))
            throw system_error(native_error(status));
        state->native_owner = state;
        state->api->SetCallbackHandler(
            stream, reinterpret_cast<void*>(&detail::msquic_stream_state::event), state.get());
        state_ = std::move(state);
    }
    msquic_stream::~msquic_stream() {
        (void)close();
    }
    error_code msquic_stream::read_start(stream_read_callback callback, size_t size) noexcept {
        auto state = state_;
        if (state->closing || state->closed)
            return errc::shutdown;
        if (!callback || size == 0)
            return errc::invalid;
        if (state->reader)
            return errc::busy;
        state->reader = std::move(callback);
        auto status = state->api->StreamReceiveSetEnabled(state->stream, TRUE);
        if (QUIC_FAILED(status)) {
            state->reader = {};
            return state->fail(status);
        }
        return {};
    }
    error_code msquic_stream::read_stop() noexcept {
        auto state = state_;
        state->reader = {};
        if (state->closed)
            return {};
        const auto status = state->api->StreamReceiveSetEnabled(state->stream, FALSE);
        return QUIC_FAILED(status) ? state->fail(status) : error_code{};
    }
    error_code msquic_stream::write(view<const shared_buffer> buffers,
                                    completion callback) noexcept {
        auto state = state_;
        if (state->closing || state->closed)
            return errc::shutdown;
        if (buffers.size() > numeric_limits<uint32_t>::max())
            return errc::too_big;
        try {
            auto operation = std::make_unique<detail::msquic_stream_state::request>();
            operation->state = state;
            operation->callback = std::move(callback);
            if (!buffers.empty())
                operation->owners.assign(buffers.begin(), buffers.end());
            for (const auto& value : operation->owners) {
                if (value.size() > numeric_limits<uint32_t>::max())
                    return errc::too_big;
                operation->native.push_back(
                    {static_cast<uint32_t>(value.size()),
                     reinterpret_cast<uint8_t*>(const_cast<byte*>(value.bytes().data()))});
            }
            // MsQuic requires at least one buffer, including empty writes.
            if (operation->native.empty())
                operation->native.push_back({0, nullptr});
            ++state->pending;
            auto status = state->api->StreamSend(state->stream, operation->native.data(),
                                                 static_cast<uint32_t>(operation->native.size()),
                                                 QUIC_SEND_FLAG_NONE, operation.get());
            if (QUIC_FAILED(status)) {
                --state->pending;
                return state->fail(status);
            }
            (void)operation.release();
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }
    error_code msquic_stream::close(close_callback callback) noexcept {
        auto state = state_;
        if (state->closed) {
            if (callback)
                callback();
            return {};
        }
        if (callback && state->on_close)
            return errc::busy;
        if (callback)
            state->on_close = std::move(callback);
        if (!state->closing) {
            state->closing = true;
            state->reader = {};
            ++state->callbacks;
            auto status = state->api->StreamShutdown(
                state->stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT | QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, 0);
            if (status == QUIC_STATUS_OUT_OF_MEMORY) {
                // Methods run on the native connection callback thread. The
                // inline abort needs no operation allocation. IMMEDIATE cannot
                // be combined with INLINE in the pinned SDK; let its ordinary
                // shutdown/timeout path finish instead.
                status = state->api->StreamShutdown(
                    state->stream,
                    QUIC_STREAM_SHUTDOWN_FLAG_ABORT | QUIC_STREAM_SHUTDOWN_FLAG_INLINE, 0);
            }
            --state->callbacks;
            if (QUIC_FAILED(status)) {
                state->closing = false;
                state->on_close = {};
                return state->fail(status);
            }
        }
        state->finish_close();
        return {};
    }
    error_info msquic_stream::error_details() const noexcept {
        return state_->error;
    }
}
