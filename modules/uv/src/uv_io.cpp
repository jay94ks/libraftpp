// C++ port of cowsql/raft uv.c and snapshot/append/close coordination;
// see LICENSE.md. The transport owns message delivery and native connections.
#include <raftpp/uv/uv_io.hpp>
#include "file_backend.hpp"
#include "tracing.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
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
    struct uv_send_operation {
        borrowed<uv_io_state> owner = nullptr;
        shared<const message> value;
        completion callback;
        error_info diagnostic;
        void operator()(error_code status) noexcept;
    };
    struct uv_io_state {
        borrowed<uv_loop_t> loop = nullptr;
        shared<disk_storage> disk;
        shared<transport> network;
        uv_io_options options;
        disk_capabilities capabilities;
        size_t probed_block_size = 0;
        resource_owned<file_backend> files;
        shared<uv_io_state> self;
        resource_list<resource_owned<uv_send_operation>> sends;
        uv_timer_t timer{};
        shared<function<void()>> tick;
        shared<function<void(message)>> receive;
        close_callback on_files_close, on_network_close, close_completion;
        error_code error;
        error_info diagnostic;
        unsigned dispatching = 0;
        bool initialized = false, loaded = false, started = false, closing = false, closed = false;
        bool storage_only = false;
        bool timer_alive = false, network_used = false, network_closed = false,
             files_closed = false;

        error_code available() const noexcept {
            return closing        ? make_error_code(errc::shutdown)
                   : !initialized ? make_error_code(errc::invalid)
                                  : error_code{};
        }
        error_code remember(error_code status, string_view operation,
                            error_info info = {}) noexcept {
            if (status)
                diagnostic = info.code == status ? wrap_error_info(info, operation)
                                                 : make_error_info(status, operation);
            return status;
        }
        template <typename... Args>
        function<void(error_code, Args...)> protect(function<void(error_code, Args...)> callback) {
            weak<uv_io_state> observer = self;
            return [observer, callback = std::move(callback)](error_code status,
                                                              Args... args) mutable noexcept {
                auto keep = observer.lock();
                if (!keep)
                    return;
                ++keep->dispatching;
                if (status)
                    keep->remember(status, "uv.complete",
                                   keep->files ? keep->files->error_details() : error_info{});
                try {
                    if (callback)
                        callback(status, std::move(args)...);
                } catch (...) {
                    keep->error = errc::io_error;
                    keep->begin_close();
                }
                --keep->dispatching;
                keep->maybe_finish();
            };
        }
        void maybe_finish() noexcept {
            if (!closing || closed || timer_alive || !network_closed || !files_closed ||
                !sends.empty() || dispatching)
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
            started = false;
            ++dispatching;
            options.tracing.log(trace_level::trace, "closing uv io");
            tick = {};
            receive = {};
            if (timer_alive) {
                (void)uv_timer_stop(&timer);
                uv_close(reinterpret_cast<uv_handle_t*>(&timer), [](uv_handle_t* native) noexcept {
                    auto keep = static_cast<uv_io_state*>(native->data)->self;
                    keep->timer_alive = false;
                    keep->maybe_finish();
                });
            }
            if (files)
                (void)files->close(std::ref(on_files_close));
            else
                files_closed = true;
            if (network_used)
                network->close(std::ref(on_network_close));
            else
                network_closed = true;
            --dispatching;
            maybe_finish();
        }
        void sent(uv_send_operation& operation, error_code status) noexcept {
            logf(options.tracing, trace_level::trace, source_location::current(),
                 "send complete: %d", status.value());
            const auto found =
                std::ranges::find_if(sends, [&](const auto& p) { return p.get() == &operation; });
            auto request = std::move(*found);
            sends.erase(found);
            request->diagnostic = status ? network->error_details() : error_info{};
            if (status)
                remember(status, "uv.send.complete", request->diagnostic);
            ++dispatching;
            try {
                if (request->callback)
                    request->callback(status);
            } catch (...) {
                error = errc::io_error;
                begin_close();
            }
            request.reset();
            --dispatching;
            maybe_finish();
        }
        void received(message value) noexcept {
            if (!started || closing)
                return;
            logf(options.tracing, trace_level::trace, source_location::current(),
                 "receive message from server %llu",
                 static_cast<unsigned long long>(value.server_id));
            ++dispatching;
            auto callback = receive;
            try {
                if (callback && *callback)
                    (*callback)(std::move(value));
            } catch (...) {
                error = errc::io_error;
                begin_close();
            }
            --dispatching;
            maybe_finish();
        }
    };
    void uv_send_operation::operator()(error_code status) noexcept {
        auto keep = owner->self;
        keep->sent(*this, status);
    }
}

namespace raftpp {
    uv_io::uv_io(uv_loop_s& loop, fs::path directory, shared<transport> network,
                 uv_io_options options)
        : state_(std::allocate_shared<detail::uv_io_state>(
              resource_allocator<detail::uv_io_state>{})) {
        state_->loop = &loop;
        state_->disk = std::allocate_shared<detail::disk_storage>(
            resource_allocator<detail::disk_storage>{}, std::move(directory));
        state_->network = std::move(network);
        state_->options = options;
        auto pointer = state_.get();
        state_->on_files_close = [pointer] {
            pointer->files_closed = true;
            pointer->maybe_finish();
        };
        state_->on_network_close = [pointer] {
            pointer->network_closed = true;
            pointer->maybe_finish();
        };
    }
    uv_io::uv_io(uv_loop_s& loop, fs::path directory, uv_io_options options)
        : uv_io(loop, std::move(directory), {}, std::move(options)) {
        state_->storage_only = true;
    }
    uv_io::~uv_io() {
        auto keep = state_;
        keep->begin_close();
    }
    error_code uv_io::init(server_id id, string_view address) noexcept {
        auto keep = state_;
        if (keep->initialized || keep->closing)
            return errc::invalid;
        keep->options.tracing.log(trace_level::trace, "initializing uv io");
        if (id == 0)
            return errc::bad_id;
        if (address.empty() || (!keep->network && !keep->storage_only) ||
            (keep->options.block_size &&
             (!std::has_single_bit(keep->options.block_size) || keep->options.block_size < 512)))
            return errc::invalid;
        keep->self = keep;
        const auto result = detail::guarded([&]() -> error_code {
            if (auto error = keep->disk->open(&keep->diagnostic))
                return error;
            if (auto error = keep->disk->probe(keep->capabilities, {}, &keep->diagnostic))
                return error;
            keep->probed_block_size = keep->capabilities.block_size;
            if (keep->options.block_size)
                keep->capabilities.block_size = keep->options.block_size;
            if (keep->options.segment_size < keep->capabilities.block_size ||
                keep->options.segment_size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
                return errc::invalid;
            if (!keep->storage_only) {
                keep->network_used = true;
                if (auto error = keep->network->init(id, address))
                    return keep->remember(error, "uv.transport.init",
                                          keep->network->error_details());
                if (uv_timer_init(keep->loop, &keep->timer) < 0)
                    return errc::io_error;
                keep->timer.data = keep.get();
                keep->timer_alive = true;
                const auto wall = chrono::duration_cast<chrono::milliseconds>(
                                      chrono::system_clock::now().time_since_epoch())
                                      .count();
                std::srand(static_cast<unsigned>(id) ^ static_cast<unsigned>(uv_now(keep->loop)) ^
                           static_cast<unsigned>(wall));
            }
            keep->initialized = true;
            return {};
        });
        if (result) {
            keep->error = result;
            if (keep->diagnostic.code != result)
                keep->diagnostic = make_error_info(result, "uv.init");
            keep->begin_close();
        }
        return result;
    }
    error_code uv_io::bootstrap(const configuration& configuration) noexcept {
        if (auto error = state_->available())
            return error;
        if (state_->files)
            return errc::busy;
        return state_->disk->bootstrap(configuration, state_->capabilities.block_size,
                                       &state_->diagnostic);
    }
    error_code uv_io::recover(const configuration& configuration) noexcept {
        if (auto error = state_->available())
            return error;
        if (state_->files)
            return errc::busy;
        return state_->disk->recover(configuration, state_->capabilities.block_size,
                                     state_->options.auto_recovery, &state_->diagnostic);
    }
    error_code uv_io::load(io_state& output) noexcept {
        auto keep = state_;
        if (auto error = keep->available())
            return error;
        if (keep->files)
            return errc::busy;
        const auto status = detail::guarded([&]() -> error_code {
            io_state result;
            if (auto error = keep->disk->load(result, keep->options.auto_recovery,
                                              keep->options.max_snapshot_size, &keep->diagnostic))
                return error;
            if (result.start_index > 10000000000000000ULL ||
                result.entries.size() > 10000000000000000ULL - result.start_index)
                return errc::too_big;
            detail::file_backend_options options;
            options.log.segment_size = keep->options.segment_size;
            options.log.disk_retry = keep->options.disk_retry;
            options.log.capabilities = keep->capabilities;
            options.max_snapshot_size = keep->options.max_snapshot_size;
            options.log.tracing = keep->options.tracing;
            keep->files =
                allocate_owned<detail::file_backend>(resource_allocator<detail::file_backend>{},
                                                     *keep->loop, keep->disk, std::move(options));
            if (auto error = keep->files->open(result.start_index + result.entries.size())) {
                keep->diagnostic = keep->files->error_details();
                return error;
            }
            output = std::move(result);
            keep->loaded = true;
            return {};
        });
        if (status) {
            keep->error = status;
            keep->begin_close();
        }
        return status;
    }
    error_code uv_io::start(unsigned interval, function<void()> tick,
                            function<void(message)> receive) noexcept {
        auto keep = state_;
        if (auto error = keep->available())
            return error;
        if (!keep->loaded || keep->started || interval == 0 || keep->storage_only)
            return errc::invalid;
        error_info startup;
        const auto status = detail::guarded([&]() -> error_code {
            weak<detail::uv_io_state> observer = keep;
            function<void(message)> handler = [observer](message value) {
                if (auto self = observer.lock())
                    self->received(std::move(value));
            };
            auto tick_owner = std::allocate_shared<function<void()>>(
                resource_allocator<function<void()>>{}, std::move(tick));
            auto receive_owner = std::allocate_shared<function<void(message)>>(
                resource_allocator<function<void(message)>>{}, std::move(receive));
            keep->tick = std::move(tick_owner);
            keep->receive = std::move(receive_owner);
            keep->started = true;
            if (auto error = keep->network->start(std::move(handler))) {
                const auto info = keep->network->error_details();
                startup = info.code == error ? wrap_error_info(info, "uv.transport.start")
                                             : make_error_info(error, "uv.transport.start");
                return error;
            }
            if (keep->closing)
                return errc::shutdown;
            return uv_timer_start(
                       &keep->timer,
                       [](uv_timer_t* native) noexcept {
                           auto self = static_cast<detail::uv_io_state*>(native->data)->self;
                           if (self->closing)
                               return;
                           ++self->dispatching;
                           auto callback = self->tick;
                           try {
                               if (callback && *callback)
                                   (*callback)();
                           } catch (...) {
                               self->error = errc::io_error;
                               self->begin_close();
                           }
                           --self->dispatching;
                           self->maybe_finish();
                       },
                       interval, interval) < 0
                       ? make_error_code(errc::io_error)
                       : error_code{};
        });
        if (status) {
            keep->remember(status, "uv.start", startup);
            keep->error = status;
            keep->begin_close();
        }
        return status;
    }
    void uv_io::close(close_callback callback) noexcept {
        auto keep = state_;
        if (keep->closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
            return;
        }
        // io/node uses one close registration. A repeated empty close is benign.
        if (callback && !keep->close_completion)
            keep->close_completion = std::move(callback);
        keep->begin_close();
    }
    error_code uv_io::set_term(term term) noexcept {
        auto keep = state_;
        if (auto error = keep->available())
            return error;
        const auto status = keep->disk->set_term(term, &keep->diagnostic);
        return keep->remember(status, "uv.set_term", keep->diagnostic);
    }
    error_code uv_io::set_vote(server_id id) noexcept {
        auto keep = state_;
        if (auto error = keep->available())
            return error;
        const auto status = keep->disk->set_vote(id, &keep->diagnostic);
        return keep->remember(status, "uv.set_vote", keep->diagnostic);
    }
    error_code uv_io::truncate(log_index index) noexcept {
        if (auto error = state_->available())
            return error;
        return state_->files ? state_->files->truncate(index) : make_error_code(errc::invalid);
    }
    error_code uv_io::append(log_index index, shared<const entry_batch> entries,
                             completion callback) noexcept {
        auto keep = state_;
        return detail::guarded([&]() -> error_code {
            if (auto error = keep->available())
                return error;
            return keep->files ? keep->files->append(index, std::move(entries),
                                                     keep->protect(std::move(callback)))
                               : make_error_code(errc::invalid);
        });
    }
    error_code uv_io::send(shared<const message> value, completion callback) noexcept {
        auto keep = state_;
        return detail::guarded([&]() -> error_code {
            if (auto error = keep->available())
                return error;
            if (!keep->started || !value)
                return errc::invalid;
            auto operation = allocate_owned<detail::uv_send_operation>(
                resource_allocator<detail::uv_send_operation>{});
            operation->owner = keep.get();
            operation->value = std::move(value);
            operation->callback = std::move(callback);
            auto pointer = operation.get();
            keep->sends.push_back(std::move(operation));
            error_code status;
            try {
                status = keep->network->send(pointer->value, std::ref(*pointer));
                if (status)
                    pointer->diagnostic = keep->network->error_details();
            } catch (const bad_alloc&) {
                status = errc::out_of_memory;
            } catch (...) {
                status = errc::io_error;
            }
            if (status) {
                keep->remember(status, "uv.send.submit", pointer->diagnostic);
                const auto found = std::ranges::find_if(
                    keep->sends, [&](const auto& p) { return p.get() == pointer; });
                if (found != keep->sends.end())
                    keep->sends.erase(found);
            }
            return status;
        });
    }
    error_code uv_io::snapshot_put(unsigned trailing, shared<const snapshot> value,
                                   completion callback) noexcept {
        auto keep = state_;
        return detail::guarded([&]() -> error_code {
            if (auto error = keep->available())
                return error;
            return keep->files ? keep->files->snapshot_put(trailing, std::move(value),
                                                           keep->protect(std::move(callback)))
                               : make_error_code(errc::invalid);
        });
    }
    error_code uv_io::snapshot_put_local(unsigned trailing, shared<const snapshot> value,
                                         completion callback) noexcept {
        auto keep = state_;
        return detail::guarded([&]() -> error_code {
            if (auto error = keep->available())
                return error;
            return keep->files ? keep->files->snapshot_put(trailing, std::move(value),
                                                           keep->protect(std::move(callback)), true)
                               : make_error_code(errc::invalid);
        });
    }
    error_code uv_io::snapshot_get(snapshot_callback callback) noexcept {
        auto keep = state_;
        return detail::guarded([&]() -> error_code {
            if (auto error = keep->available())
                return error;
            return keep->files ? keep->files->snapshot_get(keep->protect(std::move(callback)))
                               : make_error_code(errc::invalid);
        });
    }
    uint64_t uv_io::time() const noexcept {
        return uv_now(state_->loop);
    }
    uint32_t uv_io::random(uint32_t minimum, uint32_t maximum) noexcept {
        return minimum >= maximum
                   ? minimum
                   : minimum + static_cast<uint32_t>(std::rand()) % (maximum - minimum);
    }
    uint16_t uv_io::capacity() const noexcept {
        return state_->files ? static_cast<uint16_t>(state_->files->capacity() / 1024) : 0;
    }
    error_code uv_io::last_error() const noexcept {
        return state_->error   ? state_->error
               : state_->files ? state_->files->last_error()
                               : error_code{};
    }
    error_info uv_io::error_details() const noexcept {
        const auto status = last_error();
        if (state_->diagnostic.code)
            return state_->diagnostic;
        if (state_->files) {
            const auto info = state_->files->error_details();
            if (info.code)
                return wrap_error_info(info, "uv.io");
        }
        return make_error_info(status, "uv.io");
    }
    void uv_io::set_tracer(tracer value) noexcept {
        state_->options.tracing = value;
        if (state_->files)
            state_->files->set_tracer(std::move(value));
    }
    error_code uv_io::set_block_size(size_t size) noexcept {
        if (state_->closing)
            return errc::shutdown;
        if (state_->files)
            return errc::busy;
        if (size && (size < 512 || !std::has_single_bit(size)))
            return errc::invalid;
        const auto block = size ? size : state_->probed_block_size;
        if (state_->initialized && state_->options.segment_size < block)
            return errc::invalid;
        state_->options.block_size = size;
        if (state_->initialized)
            state_->capabilities.block_size = block;
        return {};
    }
    error_code uv_io::set_segment_size(size_t size) noexcept {
        if (state_->closing)
            return errc::shutdown;
        const auto block =
            state_->initialized ? state_->capabilities.block_size : state_->options.block_size;
        if (size == 0 || size < block ||
            size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
            return errc::invalid;
        if (state_->files) {
            if (auto error = state_->files->set_segment_size(size))
                return error;
        }
        state_->options.segment_size = size;
        return {};
    }
    void uv_io::set_disk_retry(unsigned msecs) noexcept {
        if (state_->closing)
            return;
        state_->options.disk_retry = msecs;
        if (state_->files)
            state_->files->set_disk_retry(msecs);
    }
    void uv_io::set_connect_retry_delay(unsigned msecs) noexcept {
        auto keep = state_;
        if (!keep->closing && keep->network)
            keep->network->set_connect_retry_delay(msecs);
    }
    void uv_io::set_auto_recovery(bool enabled) noexcept {
        if (!state_->closing)
            state_->options.auto_recovery = enabled;
    }
    error_code uv_io::set_snapshot_compression(bool) noexcept {
        return {};
    }
}
