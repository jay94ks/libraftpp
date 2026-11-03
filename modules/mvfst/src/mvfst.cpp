#include <raftpp/mvfst/mvfst.hpp>
#include <folly/io/IOBuf.h>
#include <cstring>

namespace raftpp {
    namespace {
        class mvfst_error_category final : public error_category {
        public:
            const char* name() const noexcept override {
                return "mvfst";
            }
            string message(int code) const override {
                return "mvfst local error " + std::to_string(code);
            }
        };
        const mvfst_error_category category{};
    }
    namespace detail {
        struct mvfst_stream_state : quic::StreamReadCallback,
                                    enable_shared_from_this<mvfst_stream_state> {
            shared<quic::QuicSocketLite> connection;
            quic::StreamId id;
            stream_read_callback reader;
            close_callback on_close;
            size_t callbacks = 0;
            error_info error;
            size_t read_size = 65536;
            bool registered = false, closed = false;
            mvfst_stream_state(shared<quic::QuicSocketLite> value, quic::StreamId stream)
                : connection(std::move(value)), id(stream) {}
            void finish_close() noexcept {
                if (!closed || callbacks)
                    return;
                auto callback = std::move(on_close);
                if (callback)
                    callback();
            }
            void invoke(stream_read_callback callback, error_code error,
                        shared_buffer bytes) noexcept {
                ++callbacks;
                callback(error, std::move(bytes));
                --callbacks;
                finish_close();
            }
            error_code fail(quic::LocalErrorCode cause) noexcept {
                error = make_error_info(errc::io_error, "mvfst stream");
                error.cause = std::error_code{static_cast<int>(cause), category};
                return error.code;
            }
            void readAvailable(quic::StreamId) noexcept override {
                auto self = shared_from_this();
                if (closed || !reader)
                    return;
                auto native_owner = connection;
                try {
                    auto result = native_owner->read(id, read_size);
                    if (!result) {
                        auto callback = std::move(reader);
                        auto code = fail(result.error());
                        (void)native_owner->setReadCallback(id, nullptr, std::nullopt);
                        registered = false;
                        invoke(std::move(callback), code, {});
                        return;
                    }
                    auto& [native, eof] = result.value();
                    shared_buffer bytes;
                    if (native) {
                        const auto size = native->computeChainDataLength();
                        buffer copy{size};
                        size_t offset = 0;
                        for (const auto& part : *native) {
                            if (part.size())
                                std::memcpy(copy.data() + offset, part.data(), part.size());
                            offset += part.size();
                        }
                        bytes = shared_buffer{std::move(copy)};
                    }
                    if (!bytes.empty()) {
                        auto callback = reader;
                        invoke(std::move(callback), {}, std::move(bytes));
                    }
                    if (eof && !closed && reader) {
                        auto callback = std::move(reader);
                        (void)native_owner->setReadCallback(id, nullptr, std::nullopt);
                        registered = false;
                        invoke(std::move(callback), errc::no_connection, {});
                    }
                } catch (const bad_alloc&) {
                    auto callback = std::move(reader);
                    (void)native_owner->setReadCallback(id, nullptr, std::nullopt);
                    registered = false;
                    if (callback)
                        invoke(std::move(callback), errc::out_of_memory, {});
                } catch (...) {
                    auto callback = std::move(reader);
                    (void)native_owner->setReadCallback(id, nullptr, std::nullopt);
                    registered = false;
                    if (callback)
                        invoke(std::move(callback), errc::io_error, {});
                }
            }
            void readError(quic::StreamId, quic::QuicError native_error) noexcept override {
                auto self = shared_from_this();
                if (closed || !connection)
                    return;
                auto native_owner = connection;
                auto callback = std::move(reader);
                (void)native_owner->setReadCallback(id, nullptr, std::nullopt);
                registered = false;
                error = make_error_info(errc::io_error, "mvfst read", native_error.message);
                if (callback && !closed)
                    invoke(std::move(callback), error.code, {});
            }
        };
    }
    mvfst_stream::mvfst_stream(shared<quic::QuicSocketLite> connection, quic::StreamId id) {
        if (!connection || quic::isUnidirectionalStream(id))
            throw invalid_argument("mvfst requires a bidirectional stream and connection");
        state_ = std::make_shared<detail::mvfst_stream_state>(std::move(connection), id);
    }
    mvfst_stream::~mvfst_stream() {
        (void)close();
    }
    error_code mvfst_stream::read_start(stream_read_callback callback, size_t size) noexcept {
        auto state = state_;
        if (state->closed)
            return errc::shutdown;
        if (!callback || size == 0)
            return errc::invalid;
        if (state->reader)
            return errc::busy;
        state->reader = std::move(callback);
        state->read_size = size;
        try {
            auto connection = state->connection;
            const bool registered = state->registered;
            state->registered = true;
            auto result = registered ? connection->resumeRead(state->id)
                                     : connection->setReadCallback(state->id, state.get());
            if (!result) {
                state->registered = registered;
                state->reader = {};
                return state->fail(result.error());
            }
            return {};
        } catch (const bad_alloc&) {
            state->reader = {};
            return errc::out_of_memory;
        } catch (...) {
            state->reader = {};
            return errc::io_error;
        }
    }
    error_code mvfst_stream::read_stop() noexcept {
        auto state = state_;
        state->reader = {};
        if (!state->registered || state->closed)
            return {};
        try {
            auto connection = state->connection;
            auto result = connection->pauseRead(state->id);
            return result ? error_code{} : state->fail(result.error());
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (...) {
            return errc::io_error;
        }
    }
    error_code mvfst_stream::write(view<const shared_buffer> buffers,
                                   completion callback) noexcept {
        auto state = state_;
        if (state->closed)
            return errc::shutdown;
        try {
            size_t size = 0;
            for (const auto& value : buffers) {
                if (value.size() > numeric_limits<size_t>::max() - size)
                    return errc::too_big;
                size += value.size();
            }
            auto native = folly::IOBuf::create(size);
            for (const auto& value : buffers) {
                if (value.size())
                    std::memcpy(native->writableTail(), value.bytes().data(), value.size());
                native->append(value.size());
            }
            // writeChain takes ownership of the copy. Completion is local SDK
            // acceptance, rather than a remote delivery/consensus acknowledgement.
            auto result = state->connection->writeChain(state->id, std::move(native), false);
            if (!result)
                return state->fail(result.error());
            ++state->callbacks;
            if (callback)
                callback({});
            --state->callbacks;
            state->finish_close();
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        } catch (...) {
            return errc::io_error;
        }
    }
    error_code mvfst_stream::close(close_callback callback) noexcept {
        auto state = state_;
        if (callback && state->on_close)
            return errc::busy;
        if (callback)
            state->on_close = std::move(callback);
        if (!state->closed) {
            state->closed = true;
            state->reader = {};
            auto connection = state->connection;
            if (state->registered) {
                (void)connection->setReadCallback(state->id, nullptr, std::nullopt);
                state->registered = false;
            }
            (void)connection->resetStream(state->id, quic::GenericApplicationErrorCode::NO_ERROR);
            (void)connection->stopSending(state->id, quic::GenericApplicationErrorCode::NO_ERROR);
            state->connection.reset();
        }
        state->finish_close();
        return {};
    }
    error_info mvfst_stream::error_details() const noexcept {
        return state_->error;
    }
}
