// Owning C++ stream handoff for cowsql/raft raft_uv_transport; see LICENSE.md.
#include <raftpp/uv/uv_stream.hpp>
#include <raftpp/memory.hpp>
#include "uv_error.hpp"
#include <algorithm>
namespace raftpp::detail {
    struct uv_stream_state : enable_shared_from_this<uv_stream_state> {
        struct write_operation {
            shared<uv_stream_state> owner;
            uv_write_t native{};
            resource_vector<shared_buffer> storage;
            resource_vector<uv_buf_t> buffers;
            completion callback;
        };
        borrowed<uv_stream_t> native;
        borrowed<void> previous_data;
        stream_deleter release;
        shared<uv_stream_state> self;
        shared<stream_read_callback> reader;
        buffer input;
        resource_list<resource_owned<write_operation>> writes;
        close_callback first_callback;
        resource_list<close_callback> callbacks;
        error_info diagnostic;
        bool reading = false, read_failed = false, closing = false, closed = false;
        uv_stream_state(uv_stream_t& stream, stream_deleter deleter)
            : native(&stream), previous_data(stream.data), release(std::move(deleter)) {}
        error_code note(error_code error, string_view operation, int cause = 0) noexcept {
            diagnostic = make_error_info(error, operation, {}, uv_cause(cause));
            return error;
        }
        void begin_close() noexcept {
            if (closing)
                return;
            closing = true;
            reading = false;
            reader.reset();
            (void)uv_read_stop(native);
            self = shared_from_this();
            uv_close(reinterpret_cast<uv_handle_t*>(native), [](uv_handle_t* handle) {
                auto state = static_cast<uv_stream_state*>(handle->data)->shared_from_this();
                // libuv drains canceled write callbacks before close completion.
                auto native = std::exchange(state->native, nullptr);
                native->data = state->previous_data;
                auto release = std::move(state->release);
                state->closed = true;
                try {
                    release(native);
                } catch (...) {
                    state->note(errc::invalid, "stream.release.callback");
                }
                state->self.reset();
                auto first = std::move(state->first_callback);
                auto callbacks = std::move(state->callbacks);
                try {
                    if (first)
                        first();
                } catch (...) {
                    state->note(errc::invalid, "stream.close.callback");
                }
                for (auto& callback : callbacks)
                    try {
                        if (callback)
                            callback();
                    } catch (...) {
                        state->note(errc::invalid, "stream.close.callback");
                    }
            });
        }
        void notify(error_code status, shared_buffer bytes) noexcept {
            auto callback = reader;
            if (callback && !closing)
                try {
                    (*callback)(status, std::move(bytes));
                } catch (...) {
                    note(errc::invalid, "stream.read.callback");
                    begin_close();
                }
        }
    };
}
namespace raftpp {
    uv_stream::uv_stream(uv_stream_s& stream, stream_deleter release) {
        const auto handle = reinterpret_cast<uv_handle_t*>(&stream);
        if (!release || !stream.loop || uv_is_closing(handle) ||
            (handle->type != UV_TCP && handle->type != UV_NAMED_PIPE && handle->type != UV_TTY) ||
            uv_is_active(handle))
            throw invalid_argument(
                "stream adoption requires an initialized inactive stream and a deleter");
        state_ = std::allocate_shared<detail::uv_stream_state>(
            resource_allocator<detail::uv_stream_state>{}, stream, std::move(release));
        stream.data = state_.get();
    }
    uv_stream::~uv_stream() {
        if (state_)
            state_->begin_close();
    }
    uv_stream::uv_stream(uv_stream&& other) noexcept : state_(std::move(other.state_)) {}
    uv_stream& uv_stream::operator=(uv_stream&& other) noexcept {
        if (this != &other) {
            if (state_)
                state_->begin_close();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    uv_stream::operator bool() const noexcept {
        return state_ && !state_->closing;
    }
    borrowed<uv_stream_s> uv_stream::native_handle() const noexcept {
        return state_ && !state_->closed ? state_->native : nullptr;
    }
    error_code uv_stream::read_start(stream_read_callback callback, size_t size) noexcept {
        auto state = state_;
        if (!state || state->closing)
            return errc::shutdown;
        if (state->read_failed)
            return errc::shutdown;
        if (!callback || size == 0 || size > numeric_limits<unsigned>::max())
            return state->note(errc::invalid, "stream.read.start");
        if (state->reading)
            return state->note(errc::busy, "stream.read.start");
        try {
            buffer input{size};
            auto reader = std::allocate_shared<stream_read_callback>(
                resource_allocator<stream_read_callback>{}, std::move(callback));
            state->input = std::move(input);
            state->reader = std::move(reader);
            const int status = uv_read_start(
                state->native,
                [](uv_handle_t* handle, size_t, uv_buf_t* output) {
                    auto& state = *static_cast<detail::uv_stream_state*>(handle->data);
                    output->base = reinterpret_cast<char*>(state.input.data());
                    output->len = state.input.size();
                },
                [](uv_stream_t* stream, ssize_t count, const uv_buf_t*) {
                    auto state =
                        static_cast<detail::uv_stream_state*>(stream->data)->shared_from_this();
                    if (!count || state->closing)
                        return;
                    if (count < 0) {
                        (void)uv_read_stop(stream);
                        state->reading = false;
                        state->read_failed = true;
                        const auto status = state->note(
                            count == UV_EOF ? make_error_code(errc::no_connection)
                                            : detail::native_error(static_cast<int>(count)),
                            "stream.read.complete", static_cast<int>(count));
                        state->notify(status, {});
                        return;
                    }
                    try {
                        state->notify({}, shared_buffer::copy(state->input.bytes().first(
                                              static_cast<size_t>(count))));
                    } catch (const bad_alloc&) {
                        (void)uv_read_stop(stream);
                        state->reading = false;
                        state->read_failed = true;
                        state->notify(state->note(errc::out_of_memory, "stream.read.copy"), {});
                    } catch (...) {
                        (void)uv_read_stop(stream);
                        state->reading = false;
                        state->read_failed = true;
                        state->notify(state->note(errc::invalid, "stream.read.copy"), {});
                    }
                });
            if (status) {
                state->reader.reset();
                return state->note(detail::native_error(status), "stream.read.start", status);
            }
            state->reading = true;
            return {};
        } catch (const bad_alloc&) {
            return state->note(errc::out_of_memory, "stream.read.allocate");
        } catch (const length_error&) {
            return state->note(errc::too_big, "stream.read.allocate");
        } catch (...) {
            return state->note(errc::invalid, "stream.read.allocate");
        }
    }
    error_code uv_stream::read_stop() noexcept {
        auto state = state_;
        if (!state || state->closing)
            return errc::shutdown;
        const int status = uv_read_stop(state->native);
        if (status)
            return state->note(detail::native_error(status), "stream.read.stop", status);
        state->reading = false;
        state->reader.reset();
        return {};
    }
    error_code uv_stream::write(view<const shared_buffer> buffers, completion callback) noexcept {
        auto state = state_;
        if (!state || state->closing)
            return errc::shutdown;
        if (buffers.empty() || buffers.size() > numeric_limits<unsigned>::max())
            return state->note(errc::invalid, "stream.write");
        try {
            auto operation = allocate_owned<detail::uv_stream_state::write_operation>(
                resource_allocator<detail::uv_stream_state::write_operation>{});
            operation->owner = state;
            operation->callback = std::move(callback);
            operation->storage.reserve(buffers.size());
            operation->buffers.reserve(buffers.size());
            for (const auto& value : buffers) {
                if (value.empty())
                    continue;
                if (value.size() > numeric_limits<unsigned>::max())
                    return state->note(errc::too_big, "stream.write.buffer");
                operation->storage.push_back(value);
                operation->buffers.push_back(
                    {reinterpret_cast<char*>(const_cast<byte*>(value.bytes().data())),
                     value.size()});
            }
            if (operation->buffers.empty())
                return state->note(errc::invalid, "stream.write.empty");
            auto value = operation.get();
            value->native.data = value;
            state->writes.push_back(std::move(operation));
            const int status = uv_write(
                &value->native, state->native, value->buffers.data(),
                static_cast<unsigned>(value->buffers.size()), [](uv_write_t* request, int native) {
                    auto& value =
                        *static_cast<detail::uv_stream_state::write_operation*>(request->data);
                    auto state = value.owner;
                    const auto position =
                        std::find_if(state->writes.begin(), state->writes.end(),
                                     [&](const auto& op) { return op.get() == &value; });
                    auto operation = std::move(*position);
                    state->writes.erase(position);
                    const auto status =
                        native ? (native == UV_ECANCELED ? make_error_code(errc::canceled)
                                                         : detail::native_error(native))
                               : error_code{};
                    if (status)
                        state->note(status, "stream.write.complete", native);
                    try {
                        if (operation->callback)
                            operation->callback(status);
                    } catch (...) {
                        state->note(errc::invalid, "stream.write.callback");
                        state->begin_close();
                    }
                });
            if (status) {
                state->writes.pop_back();
                return state->note(detail::native_error(status), "stream.write.submit", status);
            }
            return {};
        } catch (const bad_alloc&) {
            return state->note(errc::out_of_memory, "stream.write.allocate");
        } catch (const length_error&) {
            return state->note(errc::too_big, "stream.write.allocate");
        } catch (...) {
            return state->note(errc::invalid, "stream.write.allocate");
        }
    }
    error_code uv_stream::write(shared_buffer bytes, completion callback) noexcept {
        const array<shared_buffer, 1> buffers{std::move(bytes)};
        return write(buffers, std::move(callback));
    }
    error_code uv_stream::close(close_callback callback) noexcept {
        auto state = state_;
        if (!state || state->closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
            return {};
        }
        try {
            if (callback) {
                if (!state->first_callback)
                    state->first_callback = std::move(callback);
                else
                    state->callbacks.push_back(std::move(callback));
            }
        } catch (...) {
            return state->note(errc::out_of_memory, "stream.close.callback");
        }
        state->begin_close();
        return {};
    }
    error_info uv_stream::error_details() const noexcept {
        return state_ ? state_->diagnostic : make_error_info(errc::shutdown, "stream is empty");
    }
}
