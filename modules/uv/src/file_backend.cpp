// C++ port of cowsql/raft uv_snapshot.c and its append/barrier lifetime paths;
// see LICENSE.md. File formats and durable operations live in disk_storage.
#include "file_backend.hpp"
#include "tracing.hpp"
#include <algorithm>
#include <cstdio>
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
    }
    struct snapshot_put_operation {
        borrowed<file_backend_state> owner = nullptr;
        shared<const snapshot> value;
        prepared_snapshot prepared;
        completion callback;
        uv_work_t work{};
        error_code status;
        error_info diagnostic;
        uint64_t timestamp = 0;
        unsigned trailing = 0;
        bool local = false;
        bool working = false, waiting = false, barrier_active = false;
        void operator()(error_code status) noexcept;
    };
    struct snapshot_get_operation {
        borrowed<file_backend_state> owner = nullptr;
        snapshot_callback callback;
        optional<snapshot> value;
        uv_work_t work{};
        error_code status;
        error_info diagnostic;
    };
    struct file_backend_state {
        borrowed<uv_loop_t> loop;
        shared<disk_storage> disk;
        file_backend_options options;
        log_writer writer;
        shared<file_backend_state> self;
        resource_owned<snapshot_put_operation> put;
        resource_list<resource_owned<snapshot_get_operation>> gets;
        uv_timer_t retry{};
        close_callback on_writer_close, on_failed_writer_init, close_completion;
        error_code error;
        error_info diagnostic;
        optional<error_info> completing;
        unsigned dispatching = 0;
        bool opened = false, timer_alive = false, closing = false, closed = false,
             writer_closed = false;
        bool writer_cleanup_pending = false;

        error_code point(file_backend_operation operation) noexcept {
            return guarded([&] { return options.hook ? options.hook(operation) : error_code{}; });
        }
        void maybe_finish() noexcept {
            if (!closing || closed || !writer_closed || timer_alive || put || !gets.empty() ||
                dispatching)
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
            ++dispatching;
            if (timer_alive) {
                (void)uv_timer_stop(&retry);
                uv_close(reinterpret_cast<uv_handle_t*>(&retry), [](uv_handle_t* native) noexcept {
                    auto keep = static_cast<file_backend_state*>(native->data)->self;
                    keep->timer_alive = false;
                    keep->maybe_finish();
                });
            }
            if (put && !put->working && !put->waiting && !put->barrier_active)
                finish_put(errc::canceled);
            if (!writer_cleanup_pending)
                (void)writer.close(std::ref(on_writer_close));
            --dispatching;
            maybe_finish();
        }
        void finish_put(error_code status) noexcept {
            logf(options.log.tracing, trace_level::trace, source_location::current(),
                 "snapshot %llu put complete: %d",
                 static_cast<unsigned long long>(put->value->index), status.value());
            auto operation = std::move(put);
            const bool unblock = operation->barrier_active;
            if (status && status != errc::canceled)
                diagnostic = operation->diagnostic.code == status
                                 ? operation->diagnostic
                                 : make_error_info(status, "snapshot.put");
            // Drop anonymous descriptors before exposing completion; the payload
            // and callback remain alive throughout the call.
            operation->prepared = {};
            ++dispatching;
            const auto current =
                diagnostic.code == status ? diagnostic : make_error_info(status, "snapshot.put");
            const auto previous = std::exchange(completing, status ? optional<error_info>{current}
                                                                   : optional<error_info>{});
            try {
                if (operation->callback)
                    operation->callback(status);
            } catch (...) {
                error = errc::io_error;
                begin_close();
            }
            completing = previous;
            operation.reset();
            if (unblock)
                writer.unblock();
            --dispatching;
            maybe_finish();
        }
        static void prepare_work(uv_work_t* native) noexcept {
            auto& operation = *static_cast<snapshot_put_operation*>(native->data);
            operation.status = operation.owner->disk->prepare_snapshot(
                *operation.value, operation.timestamp, operation.prepared, &operation.diagnostic);
            operation.diagnostic = wrap_error_info(operation.diagnostic, "snapshot.prepare");
        }
        static void after_prepare(uv_work_t* native, int status) noexcept {
            auto& operation = *static_cast<snapshot_put_operation*>(native->data);
            auto keep = operation.owner->self;
            operation.working = false;
            if (keep->closing) {
                keep->finish_put(errc::canceled);
                return;
            }
            if (status < 0) {
                keep->finish_put(errc::io_error);
                return;
            }
            if (operation.status) {
                keep->diagnostic = operation.diagnostic;
                keep->options.log.tracing.log(trace_level::trace,
                                              "snapshot preparation failed -> retry");
                const auto result = uv_timer_start(
                    &keep->retry,
                    [](uv_timer_t* timer) noexcept {
                        auto keep = static_cast<file_backend_state*>(timer->data)->self;
                        if (auto error = keep->queue_prepare())
                            keep->finish_put(error);
                    },
                    keep->options.log.disk_retry, 0);
                if (result < 0)
                    keep->finish_put(errc::io_error);
                return;
            }
            operation.waiting = true;
            const auto next = !operation.local && operation.trailing == 0
                                  ? operation.value->index + 1
                                  : keep->writer.next_index();
            const auto result =
                keep->writer.barrier(next, operation.trailing == 0, std::ref(operation));
            // The callback may have run inline and destroyed the operation.
            if (result)
                keep->finish_put(result);
        }
        error_code queue_prepare() noexcept {
            if (auto error = point(file_backend_operation::queue_prepare))
                return error;
            put->work.data = put.get();
            if (uv_queue_work(loop, &put->work, prepare_work, after_prepare) < 0)
                return errc::io_error;
            put->working = true;
            return {};
        }
        void ready(snapshot_put_operation& operation, error_code status) noexcept {
            operation.waiting = false;
            if (status) {
                finish_put(status);
                return;
            }
            operation.barrier_active = true;
            if (closing) {
                finish_put(errc::canceled);
                return;
            }
            if (auto error = point(file_backend_operation::queue_publish)) {
                finish_put(error);
                return;
            }
            operation.work.data = &operation;
            const auto result = uv_queue_work(
                loop, &operation.work,
                [](uv_work_t* native) noexcept {
                    auto& operation = *static_cast<snapshot_put_operation*>(native->data);
                    auto& disk = *operation.owner->disk;
                    operation.status =
                        disk.commit_snapshot(operation.prepared, &operation.diagnostic);
                    if (!operation.status)
                        operation.status = disk.prune_snapshots(&operation.diagnostic);
                    if (!operation.status)
                        operation.status =
                            disk.prune_segments(operation.value->index, operation.trailing,
                                                &operation.diagnostic, operation.local);
                    operation.diagnostic =
                        wrap_error_info(operation.diagnostic, "snapshot.publish");
                },
                [](uv_work_t* native, int status) noexcept {
                    auto& operation = *static_cast<snapshot_put_operation*>(native->data);
                    auto keep = operation.owner->self;
                    operation.working = false;
                    keep->finish_put(status < 0 ? make_error_code(errc::io_error)
                                                : operation.status);
                });
            if (result < 0)
                finish_put(errc::io_error);
            else
                operation.working = true;
        }
        static void get_work(uv_work_t* native) noexcept {
            auto& operation = *static_cast<snapshot_get_operation*>(native->data);
            operation.status = operation.owner->disk->latest_snapshot(
                operation.value, operation.owner->options.max_snapshot_size, &operation.diagnostic);
            operation.diagnostic = wrap_error_info(operation.diagnostic, "snapshot.get");
        }
        static void after_get(uv_work_t* native, int status) noexcept {
            auto& operation = *static_cast<snapshot_get_operation*>(native->data);
            auto keep = operation.owner->self;
            const auto found = std::ranges::find_if(
                keep->gets, [&](const auto& p) { return p.get() == &operation; });
            auto request = std::move(*found);
            keep->gets.erase(found);
            ++keep->dispatching;
            const auto previous = keep->completing;
            try {
                const auto result = status < 0 ? make_error_code(errc::io_error) : request->status;
                if (result)
                    keep->diagnostic = status < 0
                                           ? make_error_info(result, "snapshot.get.complete", {},
                                                             {-status, std::generic_category()})
                                           : request->diagnostic;
                keep->completing =
                    result ? optional<error_info>{keep->diagnostic} : optional<error_info>{};
                if (request->callback)
                    request->callback(result,
                                      result ? optional<snapshot>{} : std::move(request->value));
            } catch (...) {
                keep->error = errc::io_error;
                keep->begin_close();
            }
            keep->completing = previous;
            request.reset();
            --keep->dispatching;
            keep->maybe_finish();
        }
    };
    void snapshot_put_operation::operator()(error_code status) noexcept {
        owner->ready(*this, status);
    }

    file_backend::file_backend(uv_loop_s& loop, shared<disk_storage> disk,
                               file_backend_options options)
        : state_(
              std::allocate_shared<file_backend_state>(resource_allocator<file_backend_state>{})) {
        state_->loop = &loop;
        state_->disk = std::move(disk);
        state_->options = std::move(options);
        auto pointer = state_.get();
        state_->on_writer_close = [pointer] {
            pointer->writer_closed = true;
            pointer->maybe_finish();
        };
        state_->on_failed_writer_init = [pointer] {
            pointer->writer_cleanup_pending = false;
            pointer->writer_closed = true;
            pointer->maybe_finish();
        };
    }
    file_backend::~file_backend() {
        if (state_) {
            auto keep = state_;
            keep->begin_close();
        }
    }
    error_code file_backend::open(log_index next_index) noexcept {
        auto keep = state_;
        if (keep->closing || keep->opened || !keep->disk)
            return errc::invalid;
        keep->self = keep;
        const auto status = guarded([&]() -> error_code {
            if (uv_timer_init(keep->loop, &keep->retry) < 0)
                return errc::io_error;
            keep->retry.data = keep.get();
            keep->timer_alive = true;
            keep->writer_cleanup_pending = true;
            if (auto error = log_writer::open(
                    *keep->loop, keep->disk, next_index, keep->options.log, keep->writer,
                    std::ref(keep->on_failed_writer_init), &keep->diagnostic))
                return error;
            keep->writer_cleanup_pending = false;
            keep->opened = true;
            return {};
        });
        if (status)
            keep->begin_close();
        return status;
    }
    error_code file_backend::append(log_index index, shared<const entry_batch> entries,
                                    completion callback) noexcept {
        if (!state_->opened)
            return errc::invalid;
        if (state_->closing)
            return errc::shutdown;
        return state_->writer.append(index, std::move(entries), std::move(callback));
    }
    error_code file_backend::truncate(log_index index) noexcept {
        if (!state_->opened)
            return errc::invalid;
        if (state_->closing)
            return errc::shutdown;
        return state_->writer.truncate(index);
    }
    error_code file_backend::snapshot_put(unsigned trailing, shared<const snapshot> value,
                                          completion callback, bool local) noexcept {
        auto keep = state_;
        return guarded([&]() -> error_code {
            if (!keep->opened)
                return errc::invalid;
            if (keep->closing || keep->writer.closed())
                return errc::shutdown;
            if (keep->put)
                return errc::busy;
            if (!value || value->term == 0 || value->index == 0 ||
                value->index >= 10000000000000000ULL)
                return errc::invalid;
            size_t size = 0;
            for (const auto& buffer : value->buffers) {
                if (buffer.size() > keep->options.max_snapshot_size - size)
                    return errc::too_big;
                size += buffer.size();
            }
            if (size == 0)
                return errc::invalid;
            buffer encoded;
            if (auto status = encode_snapshot_metadata(*value, encoded))
                return status;
            auto operation = allocate_owned<snapshot_put_operation>(
                resource_allocator<snapshot_put_operation>{});
            operation->owner = keep.get();
            operation->value = std::move(value);
            operation->callback = std::move(callback);
            operation->timestamp = uv_now(keep->loop);
            operation->trailing = trailing;
            operation->local = local;
            keep->put = std::move(operation);
            if (auto status = keep->queue_prepare()) {
                keep->put.reset();
                return status;
            }
            return {};
        });
    }
    error_code file_backend::snapshot_get(snapshot_callback callback) noexcept {
        auto keep = state_;
        return guarded([&]() -> error_code {
            if (!keep->opened)
                return errc::invalid;
            if (keep->closing)
                return errc::shutdown;
            auto operation = allocate_owned<snapshot_get_operation>(
                resource_allocator<snapshot_get_operation>{});
            operation->owner = keep.get();
            operation->callback = std::move(callback);
            operation->work.data = operation.get();
            keep->gets.push_back(std::move(operation));
            auto pointer = keep->gets.back().get();
            auto status = keep->point(file_backend_operation::queue_get);
            if (!status && uv_queue_work(keep->loop, &pointer->work, file_backend_state::get_work,
                                         file_backend_state::after_get) < 0)
                status = errc::io_error;
            if (status)
                keep->gets.pop_back();
            return status;
        });
    }
    error_code file_backend::close(close_callback callback) noexcept {
        auto keep = state_;
        if (keep->closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
                return errc::io_error;
            }
            return {};
        }
        if (keep->close_completion && callback)
            return errc::busy;
        if (callback)
            keep->close_completion = std::move(callback);
        keep->begin_close();
        return {};
    }
    size_t file_backend::capacity() const noexcept {
        return state_->closing ? 0 : state_->writer.capacity();
    }
    error_code file_backend::last_error() const noexcept {
        return state_->error ? state_->error : state_->writer.last_error();
    }
    error_info file_backend::error_details() const noexcept {
        if (state_->completing)
            return *state_->completing;
        if (state_->writer.last_error())
            return state_->writer.error_details();
        return state_->diagnostic.code ? state_->diagnostic : state_->writer.error_details();
    }
    bool file_backend::closed() const noexcept {
        return state_->closed;
    }
    error_code file_backend::set_segment_size(size_t size) noexcept {
        if (state_->closing)
            return errc::shutdown;
        if (state_->opened) {
            if (auto error = state_->writer.set_segment_size(size))
                return error;
        }
        state_->options.log.segment_size = size;
        return {};
    }
    void file_backend::set_disk_retry(unsigned msecs) noexcept {
        if (state_->closing)
            return;
        state_->options.log.disk_retry = msecs;
        state_->writer.set_disk_retry(msecs);
    }
    void file_backend::set_tracer(tracer value) noexcept {
        state_->options.log.tracing = value;
        state_->writer.set_tracer(std::move(value));
    }
}
