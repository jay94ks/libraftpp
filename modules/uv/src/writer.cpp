// Independent C++ port of cowsql/raft uv_writer.c; see LICENSE.md.
#include "writer.hpp"
#include "aio.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <linux/aio_abi.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#include <uv.h>

namespace raftpp::detail {
    namespace {
        template <typename F> error_code guarded(F&& action) noexcept {
            try {
                return action();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::io_error;
            }
        }
        error_code native_error(int64_t status) noexcept {
            return status == -ENOMEM ? make_error_code(errc::out_of_memory)
                                     : make_error_code(errc::io_error);
        }
        error_info diagnostic(error_code code, string_view operation, int64_t native, int fd,
                              uint64_t offset = 0, optional<uint64_t> length = {}) noexcept {
            if (!code)
                return {};
            array<char, 128> resource{};
            if (length)
                std::snprintf(
                    resource.data(), resource.size(), "fd %d offset %llu result %lld expected %llu",
                    fd, static_cast<unsigned long long>(offset), static_cast<long long>(native),
                    static_cast<unsigned long long>(*length));
            else
                std::snprintf(resource.data(), resource.size(), "fd %d offset %llu", fd,
                              static_cast<unsigned long long>(offset));
            const auto cause =
                native < 0 && native >= -static_cast<int64_t>(numeric_limits<int>::max())
                    ? error_code{static_cast<int>(-native), std::generic_category()}
                    : error_code{};
            return make_error_info(code, operation, resource.data(), cause);
        }
        using native_aio::setup;
        using native_aio::destroy;
        using native_aio::get_events;
        error_code write_result(int64_t bytes, uint64_t length) noexcept {
            if (bytes < 0)
                return errc::io_error;
            return static_cast<uint64_t>(bytes) < length ? make_error_code(errc::no_space)
                                                         : error_code{};
        }
    }
    struct write_request;
    using request_list = resource_list<resource_owned<write_request>>;
    struct write_request {
        borrowed<writer_state> writer = nullptr;
        request_list::iterator position;
        resource_vector<shared_buffer> buffers;
        resource_vector<iovec> iovecs;
        completion callback;
        iocb native{};
        uv_work_t work{};
        uint64_t length = 0;
        error_code status;
        error_info diagnostic;
        bool worker = false, context_destroyed = false;
    };
    struct writer_state {
        borrowed<uv_loop_t> loop = nullptr;
        file_handle file, event_file;
        writer_options options;
        writer_hook hook;
        aio_context_t context = 0;
        resource_vector<io_event> events;
        request_list requests;
        uv_poll_t poller{};
        uv_timer_t retry{};
        uv_work_t shutdown{};
        shared<writer_state> self;
        close_callback close_completion;
        error_code error, poll_failure, shutdown_error;
        error_info diagnostic, poll_diagnostic, shutdown_diagnostic;
        unsigned workers = 0;
        bool poll_alive = false, timer_alive = false;
        bool closing = false, closed = false, destroying = false, destroyed = false;

        int64_t point(writer_operation operation, int64_t value = 0) const noexcept {
            try {
                return hook ? hook(operation, value) : value;
            } catch (const bad_alloc&) {
                return -ENOMEM;
            } catch (...) {
                return -EIO;
            }
        }
        void notify(completion& callback, error_code status) noexcept {
            try {
                if (callback)
                    callback(status);
            } catch (...) {
                error = errc::io_error;
                begin_close();
            }
        }
        void finish(write_request& request, error_code status) noexcept {
            auto retained = std::move(*request.position);
            requests.erase(request.position);
            options.tracing.emit(trace_write_complete);
            if (status && status != errc::canceled) {
                error = status;
                diagnostic = retained->diagnostic.code == status
                                 ? retained->diagnostic
                                 : make_error_info(status, "writer.complete");
            }
            notify(retained->callback, status);
            retained.reset(); // Release payloads before close completion.
            maybe_destroy();
        }
        static void work(uv_work_t* native) noexcept {
            auto& request = *static_cast<write_request*>(native->data);
            auto& state = *request.writer;
            auto context = state.context;
            const bool separate = state.options.max_concurrent_writes > 1;
            if (separate) {
                context = 0;
                const auto status = setup(1, context);
                if (status) {
                    request.status =
                        status == -EAGAIN ? make_error_code(errc::too_many) : native_error(status);
                    request.diagnostic = detail::diagnostic(
                        request.status, "io_setup.worker", status, state.file.get(),
                        static_cast<uint64_t>(request.native.aio_offset));
                    return;
                }
            }
            auto status = state.point(writer_operation::submit_worker);
            if (status == 0)
                status = native_aio::submit(context, request.native);
            if (status) {
                request.status = errc::io_error;
                request.diagnostic =
                    detail::diagnostic(request.status, "io_submit.worker", status, state.file.get(),
                                       static_cast<uint64_t>(request.native.aio_offset));
            } else {
                io_event event{};
                const auto count = get_events(context, 1, 1, &event, nullptr);
                if (count == 1) {
                    const auto result = state.point(writer_operation::worker_result, event.res);
                    request.status = write_result(result, request.length);
                    request.diagnostic = detail::diagnostic(
                        request.status, result < 0 ? "write.worker" : "short write.worker", result,
                        state.file.get(), static_cast<uint64_t>(request.native.aio_offset),
                        request.length);
                } else {
                    request.status = errc::io_error;
                    request.diagnostic = detail::diagnostic(
                        request.status, "io_getevents.worker", count, state.file.get(),
                        static_cast<uint64_t>(request.native.aio_offset));
                    // Before releasing an outstanding kernel request's memory,
                    // destroy its context on this worker, including on errors.
                    (void)destroy(context);
                    request.context_destroyed = !separate;
                    context = 0;
                }
            }
            if (separate && context != 0)
                (void)destroy(context);
        }
        static void after_work(uv_work_t* native, int status) noexcept {
            auto& request = *static_cast<write_request*>(native->data);
            auto keep = request.writer->self;
            auto& state = *keep;
            --state.workers;
            if (request.context_destroyed) {
                state.context = 0;
                state.begin_close();
            }
            if (status < 0)
                request.diagnostic = detail::diagnostic(
                    errc::io_error, "uv_queue_work.complete", status, state.file.get(),
                    static_cast<uint64_t>(request.native.aio_offset));
            state.finish(request, status < 0 ? make_error_code(errc::io_error) : request.status);
        }
        error_code queue_worker(write_request& request) noexcept {
            request.native.aio_flags &= ~static_cast<uint32_t>(IOCB_FLAG_RESFD);
            request.native.aio_resfd = 0;
            request.native.aio_rw_flags &= ~static_cast<uint32_t>(RWF_NOWAIT);
            request.worker = true;
            request.work.data = &request;
            auto status = point(writer_operation::queue_work);
            if (status == 0)
                status = uv_queue_work(loop, &request.work, work, after_work);
            if (status != 0) {
                request.worker = false;
                request.diagnostic =
                    detail::diagnostic(native_error(status), "uv_queue_work", status, file.get(),
                                       static_cast<uint64_t>(request.native.aio_offset));
                return native_error(status);
            }
            ++workers;
            return {};
        }
        void fail_poll(string_view operation, int64_t native = 0) noexcept {
            poll_failure = errc::io_error;
            error = poll_failure;
            poll_diagnostic = diagnostic =
                detail::diagnostic(poll_failure, operation, native, event_file.get());
            begin_close();
        }
        static void poll(uv_poll_t* native, int status, int flags) noexcept {
            auto keep = static_cast<writer_state*>(native->data)->self;
            auto& state = *keep;
            if (state.closing)
                return;
            if (status < 0 || !(flags & UV_READABLE)) {
                state.fail_poll("uv_poll", status);
                return;
            }
            uint64_t count = 0;
            ssize_t read_status = -1;
            if (const auto result = state.point(writer_operation::read_event); result != 0) {
                state.fail_poll("eventfd.read", result);
                return;
            }
            do {
                read_status = ::read(state.event_file.get(), &count, sizeof(count));
            } while (read_status < 0 && errno == EINTR);
            if (read_status < 0 && errno == EAGAIN)
                return;
            if (read_status != sizeof(count)) {
                state.fail_poll("eventfd.read", read_status < 0 ? -errno : 0);
                return;
            }
            for (;;) {
                timespec timeout{};
                const auto received =
                    get_events(state.context, 0, static_cast<long>(state.events.size()),
                               state.events.data(), &timeout);
                if (received < 0) {
                    state.fail_poll("io_getevents.poll", received);
                    return;
                }
                if (received == 0)
                    return;
                for (long i = 0; i < received; ++i) {
                    const auto& event = state.events[static_cast<size_t>(i)];
                    const auto found = std::ranges::find_if(state.requests, [&](const auto& value) {
                        return reinterpret_cast<uintptr_t>(value.get()) == event.data;
                    });
                    if (found == state.requests.end() || (*found)->worker) {
                        state.fail_poll("io_getevents.identity");
                        return;
                    }
                    auto& request = **found;
                    const auto result = state.point(writer_operation::poll_result, event.res);
                    if (state.closing)
                        continue; // Shutdown retains all remaining payloads.
                    if (result == -EAGAIN) {
                        if (auto error = state.queue_worker(request))
                            state.finish(request, error);
                    } else {
                        const auto error = write_result(result, request.length);
                        request.diagnostic = detail::diagnostic(
                            error, result < 0 ? "write.poll" : "short write.poll", result,
                            state.file.get(), static_cast<uint64_t>(request.native.aio_offset),
                            request.length);
                        state.finish(request, error);
                    }
                    // Continue processing every completion, including after an
                    // EAGAIN fallback or a reentrant user close callback.
                }
                if (state.closing)
                    return;
            }
        }
        static void handle_closed(uv_handle_t* native) noexcept {
            auto keep = static_cast<writer_state*>(native->data)->self;
            if (native == reinterpret_cast<uv_handle_t*>(&keep->poller))
                keep->poll_alive = false;
            else
                keep->timer_alive = false;
            keep->maybe_destroy();
            keep->maybe_finish();
        }
        static void destroy_work(uv_work_t* native) noexcept {
            auto& state = *static_cast<writer_state*>(native->data);
            if (state.context != 0) {
                const auto result = destroy(state.context);
                if (result != 0) {
                    state.shutdown_error = errc::io_error;
                    state.shutdown_diagnostic = detail::diagnostic(
                        state.shutdown_error, "io_destroy", result, state.file.get());
                }
            }
            error_info info;
            if (auto error = state.file.close(&info)) {
                state.shutdown_error = error;
                state.shutdown_diagnostic = info;
            }
            if (auto error = state.event_file.close(&info)) {
                state.shutdown_error = error;
                state.shutdown_diagnostic = info;
            }
        }
        static void after_destroy(uv_work_t* native, int) noexcept {
            auto keep = static_cast<writer_state*>(native->data)->self;
            auto& state = *keep;
            state.context = 0;
            state.destroyed = true;
            state.destroying = false;
            if (state.shutdown_error) {
                state.error = state.shutdown_error;
                state.diagnostic = state.shutdown_diagnostic;
            }
            while (!state.requests.empty()) {
                if (state.poll_failure)
                    state.requests.front()->diagnostic = state.poll_diagnostic;
                state.finish(*state.requests.front(), state.poll_failure
                                                          ? state.poll_failure
                                                          : make_error_code(errc::canceled));
            }
            if (state.timer_alive) {
                (void)uv_timer_stop(&state.retry);
                uv_close(reinterpret_cast<uv_handle_t*>(&state.retry), handle_closed);
            }
            state.maybe_finish();
        }
        void maybe_destroy() noexcept {
            if (!closing || poll_alive || workers != 0 || destroying || destroyed)
                return;
            shutdown.data = this;
            auto status = point(writer_operation::queue_shutdown);
            if (status == 0)
                status = uv_queue_work(loop, &shutdown, destroy_work, after_destroy);
            if (status == 0) {
                destroying = true;
                return;
            }
            error = native_error(status);
            diagnostic = detail::diagnostic(error, "uv_queue_work.shutdown", status, file.get());
            // Valid uv_queue_work calls normally succeed. Retain the context
            // and payloads on transient submission failure and retry on-loop.
            if (timer_alive)
                (void)uv_timer_start(
                    &retry,
                    [](uv_timer_t* timer) noexcept {
                        static_cast<writer_state*>(timer->data)->maybe_destroy();
                    },
                    1, 0);
        }
        void maybe_finish() noexcept {
            if (!destroyed || poll_alive || timer_alive || workers != 0 || !requests.empty() ||
                closed)
                return;
            auto keep = self;
            closed = true;
            auto callback = std::move(close_completion);
            self.reset();
            try {
                if (callback)
                    callback();
            } catch (...) {
                error = errc::io_error;
            }
        }
        void begin_close() noexcept {
            if (closing)
                return;
            closing = true;
            if (poll_alive) {
                (void)uv_poll_stop(&poller);
                uv_close(reinterpret_cast<uv_handle_t*>(&poller), handle_closed);
            }
            maybe_destroy();
        }
        error_code initialization_error(string_view operation, int64_t status,
                                        bool limited = false) noexcept {
            const auto error = limited && status == -EAGAIN ? make_error_code(errc::too_many)
                                                            : native_error(status);
            diagnostic = detail::diagnostic(error, operation, status, file.get());
            return error;
        }
        error_code initialize() noexcept {
            auto status = point(writer_operation::direct);
            if (status != 0)
                return initialization_error("fcntl.direct", status);
            if (options.direct) {
                const auto flags = fcntl(file.get(), F_GETFL);
                if (flags < 0 || fcntl(file.get(), F_SETFL, flags | O_DIRECT) < 0) {
                    diagnostic =
                        detail::diagnostic(errc::io_error, "fcntl.direct", -errno, file.get());
                    return errc::io_error;
                }
            }
            status = point(writer_operation::eventfd);
            if (status != 0)
                return initialization_error("eventfd", status);
            const auto descriptor = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (descriptor < 0)
                return initialization_error("eventfd", -errno);
            event_file = file_handle{descriptor};
            status = point(writer_operation::setup);
            if (status == 0)
                status = setup(options.max_concurrent_writes, context);
            if (status != 0)
                return initialization_error("io_setup", status, true);
            // self is assigned by open before any native resource is created.
            status = uv_timer_init(loop, &retry);
            if (status != 0)
                return initialization_error("uv_timer_init", status);
            timer_alive = true;
            retry.data = this;
            status = point(writer_operation::poll_init);
            if (status == 0)
                status = uv_poll_init(loop, &poller, event_file.get());
            if (status != 0)
                return initialization_error("uv_poll_init", status);
            poll_alive = true;
            poller.data = this;
            status = point(writer_operation::poll_start);
            if (status == 0)
                status = uv_poll_start(&poller, UV_READABLE, poll);
            return status == 0 ? error_code{} : initialization_error("uv_poll_start", status);
        }
    };

    file_writer::~file_writer() {
        (void)close();
    }
    file_writer::file_writer(file_writer&&) noexcept = default;
    file_writer& file_writer::operator=(file_writer&& other) noexcept {
        if (this != &other) {
            (void)close();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    error_code file_writer::open(uv_loop_s& loop, file_handle file, writer_options options,
                                 file_writer& output, writer_hook hook,
                                 close_callback failed_init_cleanup,
                                 error_info* diagnostic) noexcept {
        error_info captured;
        const auto status = guarded([&]() -> error_code {
            if (file.get() < 0 || options.max_concurrent_writes == 0 ||
                (options.async && !options.direct))
                return errc::invalid;
            if (options.max_concurrent_writes > 65536)
                return errc::too_big;
            auto state = std::allocate_shared<writer_state>(resource_allocator<writer_state>{});
            state->loop = &loop;
            state->file = std::move(file);
            state->options = options;
            state->hook = std::move(hook);
            state->events.resize(options.max_concurrent_writes);
            state->self = state;
            if (auto error = state->initialize()) {
                captured = state->diagnostic;
                state->close_completion = std::move(failed_init_cleanup);
                state->begin_close();
                return error;
            }
            file_writer result;
            result.state_ = std::move(state);
            output = std::move(result);
            return {};
        });
        if (status && failed_init_cleanup) {
            (void)file.close();
            try {
                failed_init_cleanup();
            } catch (...) {
            }
        }
        if (diagnostic)
            *diagnostic = !status                   ? error_info{}
                          : captured.code == status ? captured
                                                    : make_error_info(status, "writer.open");
        return status;
    }
    error_code file_writer::submit(view<const shared_buffer> buffers, uint64_t offset,
                                   completion callback) noexcept {
        auto retained = state_;
        error_info captured;
        const auto result = guarded([&]() -> error_code {
            if (!retained)
                return errc::invalid;
            auto keep = retained;
            if (keep->closing)
                return errc::shutdown;
            if (buffers.empty())
                return errc::invalid;
            const auto max_iov = sysconf(_SC_IOV_MAX);
            if (buffers.size() > static_cast<size_t>(max_iov > 0 ? max_iov : 1024))
                return errc::too_big;
            if (offset > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
                return errc::too_big;
            if (keep->requests.size() >= keep->options.max_concurrent_writes)
                return errc::busy;
            auto request = allocate_owned<write_request>(resource_allocator<write_request>{});
            request->writer = keep.get();
            request->buffers.assign(buffers.begin(), buffers.end());
            request->iovecs.reserve(buffers.size());
            for (const auto& buffer : request->buffers) {
                if (buffer.size() > static_cast<uint64_t>(numeric_limits<int64_t>::max()) - offset -
                                        request->length)
                    return errc::too_big;
                request->length += buffer.size();
                request->iovecs.push_back(
                    {const_cast<byte*>(buffer.bytes().data()), buffer.size()});
            }
            request->callback = std::move(callback);
            request->native.aio_fildes = static_cast<uint32_t>(keep->file.get());
            request->native.aio_lio_opcode = IOCB_CMD_PWRITEV;
            request->native.aio_buf = reinterpret_cast<uintptr_t>(request->iovecs.data());
            request->native.aio_nbytes = request->iovecs.size();
            request->native.aio_offset = static_cast<int64_t>(offset);
            request->native.aio_data = reinterpret_cast<uintptr_t>(request.get());
            request->native.aio_rw_flags = RWF_DSYNC;
            auto& pending = *request;
            const auto position = keep->requests.insert(keep->requests.end(), std::move(request));
            pending.position = position;
            error_code error;
            bool accepted = false;
            keep->options.tracing.emit(trace_write_submit);
            if (keep->options.async) {
                pending.native.aio_flags = IOCB_FLAG_RESFD;
                pending.native.aio_resfd = static_cast<uint32_t>(keep->event_file.get());
                pending.native.aio_rw_flags |= RWF_NOWAIT;
                auto status = keep->point(writer_operation::submit_nowait);
                if (status == 0)
                    status = native_aio::submit(keep->context, pending.native);
                if (status == 0)
                    accepted = true;
                else if (status != -EAGAIN) {
                    error = native_error(status);
                    pending.diagnostic = detail::diagnostic(error, "io_submit.nowait", status,
                                                            keep->file.get(), offset);
                }
            }
            if (!accepted && !error)
                error = keep->queue_worker(pending);
            if (error) {
                captured = pending.diagnostic;
                keep->error = error;
                keep->requests.erase(position);
            }
            return error;
        });
        if (result && retained)
            retained->diagnostic =
                captured.code == result ? captured : make_error_info(result, "writer.submit");
        return result;
    }
    error_code file_writer::close(close_callback callback) noexcept {
        if (!state_ || state_->closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
                return errc::io_error;
            }
            return {};
        }
        auto keep = state_;
        if (keep->closing) {
            if (!callback)
                return {};
            // Permit registering the completion after an internal poll error.
            if (keep->close_completion)
                return errc::busy;
        }
        keep->close_completion = std::move(callback);
        keep->begin_close();
        return {};
    }
    error_code file_writer::last_error() const noexcept {
        return state_ ? state_->error : error_code{};
    }
    error_info file_writer::error_details() const noexcept {
        return state_ ? state_->diagnostic : error_info{};
    }
    bool file_writer::closed() const noexcept {
        return !state_ || state_->closed;
    }
    void file_writer::set_tracer(tracer value) noexcept {
        if (state_)
            state_->options.tracing = std::move(value);
    }

}
