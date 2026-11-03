// C++ port of cowsql/raft prepare/append/finalize/barrier coordination; see LICENSE.md.
#include "log_writer.hpp"
#include "tracing.hpp"
#include "segment_buffer.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <iterator>
#include <uv.h>

namespace raftpp::detail {
    namespace {
        error_info segment_info(error_info info, uint64_t counter, string_view operation) noexcept {
            array<char, 32> name{};
            std::snprintf(name.data(), name.size(), "open-%llu",
                          static_cast<unsigned long long>(counter));
            return wrap_error_info(wrap_error_info(info, name.data()), operation);
        }
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
    struct append_request {
        shared<const entry_batch> entries;
        shared_buffer record;
        completion callback;
        log_index index = 0;
        uint64_t epoch = 0;
    };
    struct barrier_group {
        explicit barrier_group(resource_allocator<resource_owned<completion>> allocator)
            : requests(std::move(allocator)) {}
        resource_list<resource_owned<completion>> requests;
        uint64_t boundary = 0;
        bool blocking = false, active = false;
    };
    struct alive_segment {
        explicit alive_segment(size_t block) : buffer(block), writing(pending.get_allocator()) {}
        borrowed<log_writer_state> owner = nullptr;
        uint64_t serial = 0, counter = 0;
        log_index first = 0, pending_last = 0, last = 0;
        size_t reserved = 8, written = 0;
        uint64_t offset = 0;
        uint64_t write_retry_at = 0, finalize_retry_at = 0;
        segment_buffer buffer;
        shared_buffer frozen;
        file_writer writer;
        completion on_write;
        close_callback on_close, on_failed_init;
        resource_list<resource_owned<append_request>> pending, writing;
        shared<barrier_group> barrier;
        uv_work_t work{};
        error_code status;
        error_info diagnostic;
        bool ready = false, in_flight = false, retry_write = false, seal = false;
        bool cleanup_pending = false, writer_closed = false, finalizing = false,
             retry_finalize = false;
    };
    struct prepare_operation {
        borrowed<log_writer_state> owner = nullptr;
        uint64_t counter = 0;
        size_t allocation_size = 0;
        uint64_t retry_at = 0;
        file_handle file;
        uv_work_t work{};
        error_code status;
        error_info diagnostic;
        bool ready = false, working = false, discard = false, retry = false;
    };
    struct truncate_operation {
        borrowed<log_writer_state> owner = nullptr;
        log_index index = 0;
        completion callback;
        uv_work_t work{};
        error_code status;
        error_info diagnostic;
        bool barrier_active = false;
        void operator()(error_code status) noexcept;
    };
    struct log_writer_state {
        log_writer_state() : retiring(segments.get_allocator()) {}
        borrowed<uv_loop_t> loop = nullptr;
        shared<disk_storage> disk;
        log_writer_options options;
        disk_capabilities capabilities;
        shared<log_writer_state> self;
        resource_list<resource_owned<alive_segment>> segments, retiring;
        resource_list<resource_owned<prepare_operation>> preparations;
        resource_list<resource_owned<truncate_operation>> truncations;
        resource_list<shared<barrier_group>> barriers;
        uv_timer_t timer{};
        close_callback close_completion;
        error_code error;
        error_info diagnostic;
        optional<error_info> completing;
        log_index next = 1;
        uint64_t next_counter = 1, next_serial = 1, epoch = 0;
        unsigned dispatching = 0;
        bool timer_alive = false, timer_closing = false, closing = false, closed = false;
        bool pumping = false, again = false;

        size_t allocation_size() const noexcept {
            return options.segment_size / capabilities.block_size * capabilities.block_size;
        }
        size_t pool_size() const noexcept {
            return static_cast<size_t>(std::ranges::count_if(
                preparations, [](const auto& p) { return p->ready && !p->discard; }));
        }
        void notify(completion& callback, error_code status, error_info info = {}) noexcept {
            ++dispatching;
            const auto previous = std::exchange(
                completing,
                status ? optional<error_info>{info.code == status
                                                  ? info
                                                  : make_error_info(status, "log.complete")}
                       : optional<error_info>{});
            try {
                if (callback)
                    callback(status);
            } catch (...) {
                fail(errc::io_error);
            }
            completing = previous;
            --dispatching;
        }
        void finish_requests(resource_list<resource_owned<append_request>>& source,
                             error_code status) noexcept {
            resource_list<resource_owned<append_request>> completed{source.get_allocator()};
            completed.splice(completed.end(), source);
            const auto info = diagnostic;
            if (status)
                for (const auto& request : completed)
                    if (request->epoch == epoch)
                        next = std::min(next, request->index);
            while (!completed.empty()) {
                auto request = std::move(completed.front());
                completed.pop_front();
                notify(request->callback, status, info);
            }
        }
        void retry_later() noexcept {
            if (!timer_alive || timer_closing || closing)
                return;
            const auto now = uv_now(loop);
            auto next = numeric_limits<uint64_t>::max();
            auto due = [&](bool retry, uint64_t& time) {
                if (!retry)
                    return;
                if (time == 0)
                    time = now + options.disk_retry;
                next = std::min(next, time);
            };
            for (auto& operation : preparations)
                due(operation->retry, operation->retry_at);
            for (auto& segment : segments)
                due(segment->retry_write, segment->write_retry_at);
            for (auto& segment : retiring)
                due(segment->retry_finalize, segment->finalize_retry_at);
            if (next == numeric_limits<uint64_t>::max())
                return;
            (void)uv_timer_start(
                &timer,
                [](uv_timer_t* native) noexcept {
                    auto keep = static_cast<log_writer_state*>(native->data)->self;
                    const auto now = uv_now(keep->loop);
                    for (auto& operation : keep->preparations)
                        if (operation->retry_at <= now) {
                            operation->retry = false;
                            operation->retry_at = 0;
                        }
                    for (auto& segment : keep->segments)
                        if (segment->write_retry_at <= now) {
                            segment->retry_write = false;
                            segment->write_retry_at = 0;
                        }
                    for (auto& segment : keep->retiring)
                        if (segment->finalize_retry_at <= now) {
                            segment->retry_finalize = false;
                            segment->finalize_retry_at = 0;
                        }
                    keep->pump();
                    keep->retry_later();
                },
                next > now ? next - now : 0, 0);
        }
        static void prepare_work(uv_work_t* native) noexcept {
            auto& operation = *static_cast<prepare_operation*>(native->data);
            auto& state = *operation.owner;
            if (operation.discard) {
                (void)operation.file.close();
                operation.status =
                    state.disk->finalize_segment(operation.counter, 0, 0, 0, &operation.diagnostic);
            } else
                operation.status =
                    state.disk->prepare_segment(operation.counter, operation.allocation_size,
                                                operation.file, &operation.diagnostic);
        }
        static void after_prepare(uv_work_t* native, int status) noexcept {
            auto& operation = *static_cast<prepare_operation*>(native->data);
            auto keep = operation.owner->self;
            operation.working = false;
            if (status < 0) {
                operation.status = errc::io_error;
                operation.diagnostic = make_error_info(operation.status, "segment.prepare.complete",
                                                       {}, {-status, std::generic_category()});
            }
            if (operation.status)
                keep->diagnostic = wrap_error_info(operation.diagnostic, "log.prepare");
            if (operation.discard) {
                if (operation.status)
                    keep->error = operation.status;
                const auto found = std::ranges::find_if(
                    keep->preparations, [&](const auto& p) { return p.get() == &operation; });
                keep->preparations.erase(found);
            } else if (operation.status) {
                if (keep->closing) {
                    const auto found = std::ranges::find_if(
                        keep->preparations, [&](const auto& p) { return p.get() == &operation; });
                    keep->preparations.erase(found);
                } else {
                    operation.retry = true;
                    keep->retry_later();
                }
            } else
                operation.ready = true;
            keep->pump();
        }
        error_code queue_prepare(prepare_operation& operation) noexcept {
            operation.work.data = &operation;
            // Worker inputs are immutable until completion, as in C uvIdleSegment.
            if (!operation.discard && operation.allocation_size == 0)
                operation.allocation_size = allocation_size();
            const auto status = uv_queue_work(loop, &operation.work, prepare_work, after_prepare);
            if (status < 0)
                return errc::io_error;
            operation.working = true;
            return {};
        }
        error_code replenish() {
            bool creating = false;
            for (auto& operation : preparations) {
                if (!operation->ready && !operation->discard) {
                    creating = true;
                    if (!operation->working && !operation->retry) {
                        if (auto error = queue_prepare(*operation))
                            return error;
                    }
                }
            }
            if (pool_size() >= 2 || creating)
                return {};
            if (next_counter == numeric_limits<uint64_t>::max())
                return errc::too_big;
            auto operation =
                allocate_owned<prepare_operation>(resource_allocator<prepare_operation>{});
            operation->owner = this;
            operation->counter = next_counter++;
            preparations.push_back(std::move(operation));
            return queue_prepare(*preparations.back());
        }
        error_code assign() {
            for (auto& segment : segments) {
                if (segment->counter != 0)
                    continue;
                const auto found = std::ranges::find_if(
                    preparations, [](const auto& p) { return p->ready && !p->discard; });
                if (found == preparations.end())
                    break;
                auto prepared = std::move(*found);
                preparations.erase(found);
                segment->counter = prepared->counter;
                segment->cleanup_pending = true;
                const auto status = file_writer::open(
                    *loop, std::move(prepared->file),
                    {capabilities.direct, capabilities.async, 1, options.tracing}, segment->writer,
                    options.writer, std::ref(segment->on_failed_init), &segment->diagnostic);
                if (status) {
                    diagnostic =
                        segment_info(segment->diagnostic, segment->counter, "log.writer.open");
                    return status;
                }
                segment->cleanup_pending = false;
                segment->ready = true;
            }
            return {};
        }
        void write(alive_segment& segment) noexcept {
            array<shared_buffer, 1> data{segment.frozen};
            const auto status =
                segment.writer.submit(data, segment.offset, std::ref(segment.on_write));
            if (status)
                diagnostic =
                    segment_info(segment.writer.error_details(), segment.counter, "log.append");
            if (status == errc::shutdown || status == errc::invalid)
                fail(errc::io_error);
            else if (status) {
                segment.retry_write = true;
                retry_later();
            } else
                segment.in_flight = true;
        }
        void written(alive_segment& segment, error_code status) noexcept {
            logf(options.tracing, trace_level::trace, source_location::current(),
                 "segment %llu write complete: %d",
                 static_cast<unsigned long long>(segment.counter), status.value());
            auto keep = self;
            ++dispatching;
            segment.in_flight = false;
            if (status && status != errc::canceled)
                diagnostic =
                    segment_info(segment.writer.error_details(), segment.counter, "log.append");
            if (status && !closing) {
                segment.retry_write = true;
                retry_later();
            } else {
                if (!status) {
                    const auto used = segment.buffer.size();
                    segment.written = static_cast<size_t>(segment.offset) + used;
                    segment.last = segment.pending_last;
                    if (used >= capabilities.block_size) {
                        segment.offset += used / capabilities.block_size * capabilities.block_size;
                        (void)segment.buffer.reset(used % capabilities.block_size == 0
                                                       ? 0
                                                       : used / capabilities.block_size);
                    }
                }
                segment.frozen = {};
                segment.retry_write = false;
                segment.write_retry_at = 0;
                finish_requests(segment.writing,
                                status ? make_error_code(errc::canceled) : error_code{});
            }
            --dispatching;
            pump();
        }
        static void finalize_work(uv_work_t* native) noexcept {
            auto& segment = *static_cast<alive_segment*>(native->data);
            segment.status = segment.owner->disk->finalize_segment(
                segment.counter, segment.written, segment.first, segment.last, &segment.diagnostic);
        }
        static void after_finalize(uv_work_t* native, int status) noexcept {
            auto& segment = *static_cast<alive_segment*>(native->data);
            auto keep = segment.owner->self;
            const auto error = status < 0 ? make_error_code(errc::io_error) : segment.status;
            if (error)
                keep->diagnostic = status < 0
                                       ? make_error_info(error, "segment.finalize.complete", {},
                                                         {-status, std::generic_category()})
                                       : wrap_error_info(segment.diagnostic, "log.finalize");
            keep->retiring.pop_front();
            if (error)
                keep->fail(error);
            keep->pump();
        }
        void finalize_next() noexcept {
            if (retiring.empty())
                return;
            auto& segment = *retiring.front();
            if (!segment.writer_closed || segment.finalizing || segment.retry_finalize)
                return;
            segment.work.data = &segment;
            const auto status = uv_queue_work(loop, &segment.work, finalize_work, after_finalize);
            if (status < 0) {
                segment.retry_finalize = true;
                if (closing)
                    schedule_cleanup_retry();
                else
                    retry_later();
            } else
                segment.finalizing = true;
        }
        void retire_front() noexcept {
            auto& segment = *segments.front();
            retiring.splice(retiring.end(), segments, segments.begin());
            if (segment.writer.closed())
                segment.writer_closed = true;
            else
                (void)segment.writer.close(std::ref(segment.on_close));
            finalize_next();
            again = true;
        }
        void schedule_cleanup_retry() noexcept {
            if (!timer_alive || timer_closing)
                return;
            (void)uv_timer_start(
                &timer,
                [](uv_timer_t* native) noexcept {
                    auto keep = static_cast<log_writer_state*>(native->data)->self;
                    for (auto& operation : keep->preparations)
                        operation->retry = false;
                    for (auto& segment : keep->retiring)
                        segment->retry_finalize = false;
                    keep->pump();
                },
                1, 0);
        }
        bool barrier_ready(const barrier_group& group) const noexcept {
            const auto before = [&](const auto& segment) {
                return segment->serial <= group.boundary;
            };
            return !std::ranges::any_of(segments, before) && !std::ranges::any_of(retiring, before);
        }
        void fire_barrier() noexcept {
            if (barriers.empty())
                return;
            auto group = barriers.front();
            if (group->active || !barrier_ready(*group))
                return;
            if (group->requests.empty()) {
                barriers.pop_front();
                again = true;
                return;
            }
            auto callback = std::move(group->requests.front());
            group->requests.pop_front();
            group->active = true;
            ++dispatching;
            try {
                if (*callback)
                    (*callback)({});
            } catch (...) {
                group->active = false;
                fail(errc::io_error);
            }
            --dispatching;
        }
        void resume() noexcept {
            if (barriers.empty() || !barriers.front()->active)
                return;
            barriers.front()->active = false;
            if (barriers.front()->requests.empty())
                barriers.pop_front();
            pump();
        }
        void finish_truncate(truncate_operation& operation, error_code status) noexcept {
            const auto found = std::ranges::find_if(
                truncations, [&](const auto& value) { return value.get() == &operation; });
            auto retained = std::move(*found);
            truncations.erase(found);
            ++dispatching;
            if (status && status != errc::canceled) {
                diagnostic = operation.diagnostic.code == status
                                 ? wrap_error_info(operation.diagnostic, "log.truncate")
                                 : make_error_info(status, "log.truncate");
                fail(status);
            }
            notify(retained->callback, status, operation.diagnostic);
            if (retained->barrier_active)
                resume();
            --dispatching;
            pump();
        }
        void truncate_ready(truncate_operation& operation, error_code status) noexcept {
            if (status) {
                finish_truncate(operation, status);
                return;
            }
            operation.barrier_active = true;
            operation.work.data = &operation;
            const auto queued = uv_queue_work(
                loop, &operation.work,
                [](uv_work_t* native) noexcept {
                    auto& request = *static_cast<truncate_operation*>(native->data);
                    request.status =
                        request.owner->disk->truncate(request.index, &request.diagnostic);
                },
                [](uv_work_t* native, int status) noexcept {
                    auto& request = *static_cast<truncate_operation*>(native->data);
                    auto keep = request.owner->self;
                    keep->finish_truncate(request, status < 0 ? make_error_code(errc::io_error)
                                                              : request.status);
                });
            if (queued < 0)
                finish_truncate(operation, errc::io_error);
        }
        error_code step() {
            if (closing) {
                for (auto it = preparations.begin(); it != preparations.end();) {
                    auto& operation = **it;
                    if (operation.working) {
                        ++it;
                        continue;
                    }
                    if (!operation.ready && !operation.discard) {
                        it = preparations.erase(it);
                        continue;
                    }
                    operation.discard = true;
                    if (!operation.retry) {
                        if (queue_prepare(operation)) {
                            operation.retry = true;
                            schedule_cleanup_retry();
                        }
                    }
                    ++it;
                }
            } else {
                if (auto error = assign())
                    return error;
                if (auto error = replenish())
                    return error;
            }
            while (!segments.empty()) {
                auto& segment = *segments.front();
                if (segment.counter == 0 && closing) {
                    segments.pop_front();
                    continue;
                }
                if (segment.cleanup_pending)
                    break;
                if (segment.in_flight)
                    break;
                if (!segment.writing.empty()) {
                    if (closing) {
                        finish_requests(segment.writing, errc::canceled);
                        segment.frozen = {};
                        segment.retry_write = false;
                    } else if (!segment.retry_write)
                        write(segment);
                    if (!closing)
                        break;
                }
                if ((closing || segment.seal) && segment.pending.empty()) {
                    retire_front();
                    continue;
                }
                if (!segment.ready || segment.pending.empty() || closing)
                    break;
                if (!barriers.empty() && barriers.front()->blocking &&
                    segment.barrier != barriers.front())
                    break;
                if (segment.buffer.size() == 0 && segment.offset == 0)
                    if (auto error = segment.buffer.format())
                        return error;
                for (const auto& request : segment.pending) {
                    if (auto error = segment.buffer.append(request->record.bytes()))
                        return error;
                    segment.pending_last += request->entries->size();
                }
                if (auto error = segment.buffer.freeze(segment.frozen))
                    return error;
                segment.writing.splice(segment.writing.end(), segment.pending);
                write(segment);
                break;
            }
            finalize_next();
            if (!closing)
                fire_barrier();
            return {};
        }
        void pump() noexcept {
            if (pumping || dispatching != 0) {
                again = true;
                return;
            }
            pumping = true;
            do {
                again = false;
                if (auto status = guarded([&] { return step(); }))
                    fail(status);
            } while (again);
            pumping = false;
            maybe_finish();
        }
        void fail(error_code status) noexcept {
            error = status;
            if (diagnostic.code != status)
                diagnostic = make_error_info(status, "log.writer");
            begin_close(status);
        }
        void begin_close(error_code pending_status = errc::canceled) noexcept {
            if (closing)
                return;
            closing = true;
            ++dispatching;
            if (timer_alive)
                (void)uv_timer_stop(&timer);
            for (auto& segment : retiring)
                segment->retry_finalize = false;
            for (auto& operation : preparations)
                operation->retry = false;
            for (auto& segment : segments) {
                segment->seal = true;
                finish_requests(segment->pending, pending_status);
                if (!segment->in_flight) {
                    finish_requests(segment->writing, pending_status);
                    segment->retry_write = false;
                }
            }
            resource_list<resource_owned<completion>> canceled{barriers.get_allocator()};
            for (auto& group : barriers)
                canceled.splice(canceled.end(), group->requests);
            if (!barriers.empty() && barriers.front()->active)
                barriers.erase(std::next(barriers.begin()), barriers.end());
            else
                barriers.clear();
            while (!canceled.empty()) {
                auto callback = std::move(canceled.front());
                canceled.pop_front();
                notify(*callback, errc::canceled);
            }
            --dispatching;
            pump();
        }
        void maybe_finish() noexcept {
            if (!closing || closed || !segments.empty() || !retiring.empty() ||
                !preparations.empty() || !barriers.empty() || !truncations.empty())
                return;
            if (timer_alive) {
                if (!timer_closing) {
                    timer_closing = true;
                    (void)uv_timer_stop(&timer);
                    uv_close(reinterpret_cast<uv_handle_t*>(&timer),
                             [](uv_handle_t* native) noexcept {
                                 auto keep = static_cast<log_writer_state*>(native->data)->self;
                                 keep->timer_alive = false;
                                 keep->maybe_finish();
                             });
                }
                return;
            }
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
    };

    void truncate_operation::operator()(error_code status) noexcept {
        owner->truncate_ready(*this, status);
    }

    log_writer::~log_writer() {
        (void)close();
    }
    log_writer::log_writer(log_writer&&) noexcept = default;
    log_writer& log_writer::operator=(log_writer&& other) noexcept {
        if (this != &other) {
            (void)close();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    error_code log_writer::open(uv_loop_s& loop, shared<disk_storage> disk, log_index next,
                                log_writer_options options, log_writer& output,
                                close_callback failed_init_cleanup,
                                error_info* diagnostic) noexcept {
        error_info captured;
        shared<log_writer_state> state;
        const auto status = guarded([&]() -> error_code {
            if (!disk || next == 0 || next > 10000000000000000ULL)
                return errc::invalid;
            disk_capabilities capabilities;
            if (options.capabilities)
                capabilities = *options.capabilities;
            else if (auto error = disk->probe(capabilities, {}, &captured))
                return error;
            if (capabilities.block_size < 512 || !std::has_single_bit(capabilities.block_size) ||
                (capabilities.async && !capabilities.direct) ||
                options.segment_size < capabilities.block_size ||
                options.segment_size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
                return errc::invalid;
            vector<segment_file> files;
            if (auto error = disk->list_segments(files, &captured))
                return error;
            if (std::ranges::any_of(files, [](const auto& file) { return file.is_open; }))
                return errc::busy;
            state = std::allocate_shared<log_writer_state>(resource_allocator<log_writer_state>{});
            state->loop = &loop;
            state->disk = std::move(disk);
            state->options = std::move(options);
            state->capabilities = capabilities;
            state->next = next;
            state->self = state;
            if (uv_timer_init(&loop, &state->timer) != 0)
                return errc::io_error;
            state->timer.data = state.get();
            state->timer_alive = true;
            for (unsigned i = 0; i < 2; ++i) {
                auto operation =
                    allocate_owned<prepare_operation>(resource_allocator<prepare_operation>{});
                operation->owner = state.get();
                operation->counter = state->next_counter;
                // Allocate the owning list node before touching the filesystem.
                state->preparations.push_back(std::move(operation));
                auto& prepared = *state->preparations.back();
                const auto error =
                    state->disk->prepare_segment(prepared.counter, state->allocation_size(),
                                                 prepared.file, &prepared.diagnostic);
                if (error) {
                    state->diagnostic = prepared.diagnostic;
                    state->preparations.pop_back();
                    break;
                }
                prepared.ready = true;
                ++state->next_counter;
            }
            log_writer result;
            result.state_ = state;
            output = std::move(result);
            return {};
        });
        if (status) {
            if (state) {
                state->close_completion = std::move(failed_init_cleanup);
                state->begin_close();
            } else {
                try {
                    if (failed_init_cleanup)
                        failed_init_cleanup();
                } catch (...) {
                }
            }
        }
        if (diagnostic)
            *diagnostic = !status                   ? error_info{}
                          : captured.code == status ? captured
                          : state && state->diagnostic.code == status
                              ? state->diagnostic
                              : make_error_info(status, "log.open");
        return status;
    }
    error_code log_writer::append(log_index index, shared<const entry_batch> entries,
                                  completion callback) noexcept {
        auto retained = state_;
        const auto status = guarded([&]() -> error_code {
            if (!state_)
                return errc::invalid;
            auto keep = state_;
            if (keep->closing)
                return errc::shutdown;
            if (!entries || entries->empty() || index != keep->next)
                return errc::invalid;
            if (entries->size() > 10000000000000000ULL - index)
                return errc::too_big;
            resource_vector<entry> values;
            values.reserve(entries->size());
            for (const auto& entry : *entries) {
                if (!entry)
                    return errc::invalid;
                values.push_back(*entry);
            }
            buffer encoded;
            encoding_limits limits{numeric_limits<size_t>::max(), numeric_limits<size_t>::max(),
                                   262144};
            if (auto error = encode_record(values, encoded, limits))
                return error;
            auto request = allocate_owned<append_request>(resource_allocator<append_request>{});
            request->entries = std::move(entries);
            request->record = shared_buffer{std::move(encoded)};
            request->callback = std::move(callback);
            request->index = index;
            request->epoch = keep->epoch;
            const auto size = request->record.size();
            if (size > numeric_limits<size_t>::max() - 8)
                return errc::too_big;
            alive_segment* target = keep->segments.empty() ? nullptr : keep->segments.back().get();
            const bool fits = target && !target->seal &&
                              size <= keep->options.segment_size -
                                          std::min(target->reserved, keep->options.segment_size);
            if (!fits) {
                if (keep->next_serial == numeric_limits<uint64_t>::max())
                    return errc::too_big;
                auto next = allocate_owned<alive_segment>(resource_allocator<alive_segment>{},
                                                          keep->capabilities.block_size);
                next->owner = keep.get();
                next->serial = keep->next_serial;
                next->first = index;
                next->pending_last = index - 1;
                auto pointer = next.get();
                next->on_write = [pointer](auto status) {
                    pointer->owner->written(*pointer, status);
                };
                next->on_close = [pointer] {
                    pointer->writer_closed = true;
                    pointer->owner->pump();
                };
                next->on_failed_init = [pointer] {
                    pointer->cleanup_pending = false;
                    pointer->owner->pump();
                };
                next->pending.push_back(std::move(request));
                next->reserved += size;
                keep->segments.push_back(std::move(next));
                if (target)
                    target->seal = true;
                ++keep->next_serial;
            } else {
                target->pending.push_back(std::move(request));
                target->reserved += size;
            }
            keep->next += values.size();
            keep->pump();
            return {}; // Ownership accepted; all later errors use completion.
        });
        if (status && retained)
            retained->diagnostic = make_error_info(status, "log.append.submit");
        return status;
    }
    error_code log_writer::barrier(log_index next, bool blocking, completion ready) noexcept {
        return guarded([&]() -> error_code {
            if (!state_ || next == 0 || next > 10000000000000000ULL)
                return errc::invalid;
            auto keep = state_;
            if (keep->closing)
                return errc::shutdown;
            if (keep->epoch == numeric_limits<uint64_t>::max())
                return errc::too_big;
            const bool unmarked = std::ranges::any_of(
                keep->segments, [](const auto& segment) { return !segment->barrier; });
            auto callback =
                allocate_owned<completion>(resource_allocator<completion>{}, std::move(ready));
            shared<barrier_group> group;
            if (unmarked || keep->barriers.empty()) {
                // All groups share the state queue's resource, so close can splice
                // their requests into one allocation-free cancellation list.
                group = std::allocate_shared<barrier_group>(
                    resource_allocator<barrier_group>{},
                    resource_allocator<resource_owned<completion>>{keep->barriers.get_allocator()});
                group->boundary = keep->next_serial - 1;
                group->requests.push_back(std::move(callback));
                keep->barriers.push_back(group);
            } else {
                group = keep->barriers.back();
                group->requests.push_back(std::move(callback));
            }
            group->blocking |= blocking;
            if (blocking)
                for (auto& previous : keep->barriers)
                    previous->blocking = true;
            for (auto& segment : keep->segments)
                if (!segment->barrier) {
                    segment->barrier = group;
                    segment->seal = true;
                }
            keep->next = next;
            ++keep->epoch;
            keep->pump();
            return {};
        });
    }
    error_code log_writer::truncate(log_index index, completion callback) noexcept {
        return guarded([&]() -> error_code {
            if (!state_ || index == 0)
                return errc::invalid;
            auto keep = state_;
            if (keep->closing)
                return errc::shutdown;
            if (index >= keep->next) {
                keep->notify(callback, {});
                keep->pump();
                return {};
            }
            auto operation =
                allocate_owned<truncate_operation>(resource_allocator<truncate_operation>{});
            operation->owner = keep.get();
            operation->index = index;
            operation->callback = std::move(callback);
            auto pointer = operation.get();
            keep->truncations.push_back(std::move(operation));
            const auto status = barrier(index, true, std::ref(*pointer));
            if (status) {
                const auto found = std::ranges::find_if(
                    keep->truncations, [&](const auto& value) { return value.get() == pointer; });
                keep->truncations.erase(found);
            }
            return status;
        });
    }
    void log_writer::unblock() noexcept {
        if (state_) {
            auto keep = state_;
            keep->resume();
        }
    }
    error_code log_writer::close(close_callback callback) noexcept {
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
        if (keep->closing && !callback)
            return {};
        if (keep->close_completion)
            return errc::busy;
        keep->close_completion = std::move(callback);
        keep->begin_close();
        return {};
    }
    size_t log_writer::capacity() const noexcept {
        if (!state_ || state_->closing)
            return 0;
        size_t value = 0;
        // Preserve the reference's unsigned reporting arithmetic. uv_io divides
        // this value into KiB and converts to the sixteen-bit protocol field;
        // saturation here would change capacity-threshold admission decisions.
        for (size_t i = 0; i < state_->pool_size(); ++i)
            value += state_->options.segment_size;
        for (const auto& segment : state_->segments)
            if (segment->ready)
                value += state_->options.segment_size -
                         std::min(segment->reserved, state_->options.segment_size);
        return value;
    }
    log_index log_writer::next_index() const noexcept {
        return state_ ? state_->next : 1;
    }
    error_code log_writer::last_error() const noexcept {
        return state_ ? state_->error : error_code{};
    }
    error_info log_writer::error_details() const noexcept {
        if (state_ && state_->completing)
            return *state_->completing;
        return state_ && state_->diagnostic.code ? state_->diagnostic
                                                 : make_error_info(last_error(), "log.writer");
    }
    bool log_writer::closed() const noexcept {
        return !state_ || state_->closed;
    }
    error_code log_writer::set_segment_size(size_t size) noexcept {
        if (!state_)
            return errc::invalid;
        if (state_->closing)
            return errc::shutdown;
        if (size < state_->capabilities.block_size ||
            size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
            return errc::invalid;
        state_->options.segment_size = size;
        return {};
    }
    void log_writer::set_disk_retry(unsigned msecs) noexcept {
        if (state_ && !state_->closing)
            state_->options.disk_retry = msecs;
    }
    void log_writer::set_tracer(tracer value) noexcept {
        if (!state_)
            return;
        state_->options.tracing = value;
        for (auto& segment : state_->segments)
            segment->writer.set_tracer(value);
        for (auto& segment : state_->retiring)
            segment->writer.set_tracer(value);
    }
}
