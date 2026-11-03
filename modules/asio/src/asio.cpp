#include <raftpp/asio/asio.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/post.hpp>
#include <asio/write.hpp>
#include <stdexcept>

namespace raftpp {
    asio_executor::asio_executor(asio::any_io_executor executor) : executor_(std::move(executor)) {}
    error_code asio_executor::post(function<void()> callback) noexcept {
        if (!callback)
            return errc::invalid;
        try {
            asio::post(executor_, std::move(callback));
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (...) {
            return errc::io_error;
        }
    }
    namespace detail {
        struct asio_stream_state : std::enable_shared_from_this<asio_stream_state> {
            asio::ip::tcp::socket socket;
            asio::cancellation_signal cancel_read;
            stream_read_callback reader;
            shared<buffer> unread;
            error_code read_failure;
            bool delivering = false, read_failed = false;
            size_t callbacks = 0;
            close_callback on_close;
            error_info error;
            size_t read_size = 65536;
            bool reading = false, writing = false, closing = false, closed = false;
            explicit asio_stream_state(asio::ip::tcp::socket value) : socket(std::move(value)) {}
            error_code record(const std::error_code& cause) noexcept {
                if (!cause)
                    return {};
                const auto code = cause == asio::error::operation_aborted ? errc::canceled
                                  : cause == asio::error::eof ||
                                          cause == asio::error::connection_reset ||
                                          cause == asio::error::broken_pipe
                                      ? errc::no_connection
                                      : errc::io_error;
                error = make_error_info(code, "asio stream", {}, cause);
                return code;
            }
            void finish_close() noexcept {
                if (!closing || closed || reading || writing || delivering || callbacks)
                    return;
                closed = true;
                auto callback = std::move(on_close);
                if (callback)
                    callback();
            }
            error_code read() noexcept {
                if (closing || delivering || !reader)
                    return {};
                try {
                    if (unread) {
                        // Stop/resume must preserve bytes already consumed by an
                        // immediate native read whose completion was queued.
                        auto value = shared_buffer::copy(unread->bytes());
                        auto callback = reader;
                        unread.reset();
                        delivering = true;
                        callback({}, std::move(value));
                        delivering = false;
                        if (closing || !reader)
                            return {};
                    }
                    if (read_failure) {
                        auto callback = std::move(reader);
                        read_failed = true;
                        delivering = true;
                        callback(read_failure, {});
                        delivering = false;
                        return {};
                    }
                    if (reading)
                        return {};
                    auto bytes = std::make_shared<buffer>(read_size);
                    reading = true;
                    socket.async_read_some(
                        asio::buffer(bytes->data(), bytes->size()),
                        asio::bind_cancellation_slot(
                            cancel_read.slot(), [self = shared_from_this(),
                                                 bytes](std::error_code cause, size_t count) {
                                self->reading = false;
                                if (!self->closing) {
                                    if (count) {
                                        bytes->resize(count);
                                        self->unread = bytes;
                                    }
                                    if (cause && cause != asio::error::operation_aborted)
                                        self->read_failure = self->record(cause);
                                    if (auto error = self->read()) {
                                        auto callback = std::move(self->reader);
                                        if (callback)
                                            callback(error, {});
                                    }
                                }
                                self->finish_close();
                            }));
                    return {};
                } catch (const bad_alloc&) {
                    reading = false;
                    return errc::out_of_memory;
                } catch (const std::system_error& failure) {
                    reading = false;
                    return record(failure.code());
                } catch (...) {
                    reading = false;
                    return errc::io_error;
                }
            }
        };
    }
    asio_stream::asio_stream(asio::ip::tcp::socket socket)
        : state_(std::make_shared<detail::asio_stream_state>(std::move(socket))) {
        if (!state_->socket.is_open())
            throw std::invalid_argument("closed asio socket");
    }
    asio_stream::~asio_stream() {
        (void)close();
    }
    error_code asio_stream::read_start(stream_read_callback callback, size_t size) noexcept {
        auto state = state_;
        if (state->closing || state->read_failed)
            return errc::shutdown;
        if (!callback || size == 0)
            return errc::invalid;
        if (state->reader)
            return errc::busy;
        state->reader = std::move(callback);
        state->read_size = size;
        if (auto error = state->read()) {
            state->reader = {};
            return error;
        }
        state->finish_close();
        return {};
    }
    error_code asio_stream::read_stop() noexcept {
        auto state = state_;
        state->reader = {};
        if (state->reading)
            state->cancel_read.emit(asio::cancellation_type::terminal);
        return {};
    }
    error_code asio_stream::write(view<const shared_buffer> buffers, completion callback) noexcept {
        auto state = state_;
        if (state->closing)
            return errc::shutdown;
        if (state->writing)
            return errc::busy;
        try {
            struct request {
                vector<shared_buffer> owners;
                vector<asio::const_buffer> native;
                completion done;
            };
            auto operation = std::make_shared<request>();
            operation->done = std::move(callback);
            if (!buffers.empty())
                operation->owners.assign(buffers.begin(), buffers.end());
            for (const auto& value : operation->owners)
                operation->native.emplace_back(value.bytes().data(), value.size());
            state->writing = true;
            asio::async_write(state->socket, operation->native,
                              [state, operation](std::error_code cause, size_t) mutable {
                                  state->writing = false;
                                  auto error = state->record(cause);
                                  ++state->callbacks;
                                  if (operation->done)
                                      operation->done(error);
                                  --state->callbacks;
                                  operation.reset();
                                  state->finish_close();
                              });
            return {};
        } catch (const bad_alloc&) {
            state->writing = false;
            return errc::out_of_memory;
        } catch (const std::system_error& failure) {
            state->writing = false;
            return state->record(failure.code());
        } catch (...) {
            state->writing = false;
            return errc::io_error;
        }
    }
    error_code asio_stream::close(close_callback callback) noexcept {
        auto state = state_;
        if (state->on_close && callback)
            return errc::busy;
        if (state->closed) {
            if (callback)
                callback();
            return {};
        }
        if (callback)
            state->on_close = std::move(callback);
        if (!state->closing) {
            state->closing = true;
            state->reader = {};
            state->unread.reset();
            std::error_code ignored;
            state->socket.close(ignored);
        }
        state->finish_close();
        return {};
    }
    error_info asio_stream::error_details() const noexcept {
        return state_->error;
    }
}
