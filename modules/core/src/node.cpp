// C++ port of cowsql/raft src/legacy.c; see LICENSE.md.
#include <raftpp/node.hpp>
#include <raftpp/core.hpp>
#include <raftpp/detail/core.hpp>
#include "election.hpp"
#include "tracing.hpp"
#include "fixture_internal.hpp"

#include <algorithm>
#include <climits>

namespace raftpp::detail {

    class node_impl : public enable_shared_from_this<node_impl> {
    public:
        struct request {
            entry_type type = entry_type::command;
            log_index index = 0;
            server_id promotee = 0;
            bool transfer = false, accepted = false, finished = false;
            error_code status;
            shared<void> result;
            apply_callback applied;
            completion completed;
            close_callback transferred;
        };
        struct operation {
            shared<node_impl> owner;
            completion done;
            error_code status;
            error_info diagnostic;
            bool ready = false;
        };
        struct snapshot_job {
            shared<raftpp::snapshot> value;
            event_payload event;
        };

        core_state core;
        shared<io> backend;
        shared<state_machine> fsm;
        node_options options;
        log_index applied = 0;
        bool starting = false, running = false, handling = false, draining = false, closing = false,
             closed = false, backend_closed = false;
        bool taking_snapshot = false;
        error_code failure;
        error_info failure_details, io_failure;
        resource_list<shared<request>> requests;
        resource_list<shared<operation>> operations;
        resource_deque<event_payload> events;
        resource_deque<event_payload> internal_events;
        resource_list<close_callback> close_callbacks;
        close_callback backend_close;
        shared<const step_callback> step_observer;
        shared<node_impl> closing_owner;
        shared<snapshot_job> pending_install;
        shared<const raftpp::snapshot> restoring;

        void record(error_code status, string_view where) noexcept {
            if (closing && failure)
                return; // Preserve the failure that initiated shutdown.
            failure = status;
            failure_details = io_failure.code == status ? wrap_error_info(io_failure, where)
                                                        : make_error_info(status, where);
            logf(core.tracing, trace_level::error, source_location::current(), "%.*s: error %d",
                 static_cast<int>(where.size()), where.data(), status.value());
        }
        error_code capture_io(error_code status) noexcept {
            io_failure = status ? backend->error_details() : error_info{};
            if (status && io_failure.code != status)
                io_failure = make_error_info(status);
            return status;
        }
        void finish(const shared<request>& req, error_code status,
                    shared<void> result = {}) noexcept {
            if (req->finished)
                return;
            req->finished = true;
            req->status = status;
            req->result = std::move(result);
        }
        void fail_pending(error_code status) noexcept {
            for (const auto& req : requests)
                if (!req->transfer)
                    finish(req, status);
        }
        void fail(error_code status, string_view where) noexcept {
            record(status, where);
            begin_close();
        }
        void begin_close() noexcept {
            if (closing)
                return;
            closing = true;
            running = false;
            core.tracing.log(trace_level::trace, "closing node");
            fail_pending(errc::leadership_lost);
            for (const auto& req : requests)
                if (req->transfer)
                    finish(req, {});
            events.clear();
            internal_events.clear();
            pending_install.reset();
            closing_owner = shared_from_this();
            backend->close(std::move(backend_close));
            pump();
        }

        template <typename Submit> error_code launch(Submit&& submit, completion done) {
            auto op = std::allocate_shared<operation>(resource_allocator<operation>{});
            op->owner = shared_from_this();
            op->done = std::move(done);
            completion callback = [op](error_code status) noexcept {
                if (op->ready)
                    return;
                op->diagnostic = status ? op->owner->backend->error_details() : error_info{};
                op->status = status;
                op->ready = true;
                op->owner->pump();
            };
            operations.push_back(op);
            error_code status;
            try {
                status = std::invoke(std::forward<Submit>(submit), std::move(callback));
            } catch (const bad_alloc&) {
                status = errc::out_of_memory;
            } catch (...) {
                status = errc::io_error;
            }
            if (status && !op->ready)
                operations.remove(op);
            return op->ready ? error_code{} : capture_io(status);
        }

        void enqueue(event_payload event) noexcept {
            if (closing)
                return;
            try {
                events.push_back(std::move(event));
            } catch (const bad_alloc&) {
                fail(errc::out_of_memory, "event queue");
            } catch (...) {
                fail(errc::io_error, "event queue");
            }
            pump();
        }

        void pump() noexcept {
            if (handling || draining)
                return;
            auto keep_alive = shared_from_this();
            draining = true;
            for (;;) {
                auto ready = std::find_if(operations.begin(), operations.end(),
                                          [](const auto& op) { return op->ready; });
                if (ready != operations.end()) {
                    auto op = std::move(*ready);
                    operations.erase(ready);
                    io_failure = op->diagnostic;
                    try {
                        op->done(op->status);
                    } catch (const bad_alloc&) {
                        fail(errc::out_of_memory, "completion");
                    } catch (...) {
                        fail(errc::io_error, "completion");
                    }
                    io_failure = {};
                    continue;
                }
                if (!closing && (!internal_events.empty() || !events.empty())) {
                    auto& queue = internal_events.empty() ? events : internal_events;
                    auto event = std::move(queue.front());
                    queue.pop_front();
                    const auto status = process(std::move(event));
                    if (status)
                        fail(status, "raft event");
                    continue;
                }
                auto completed =
                    std::find_if(requests.begin(), requests.end(),
                                 [](const auto& req) { return req->accepted && req->finished; });
                if (completed != requests.end()) {
                    auto req = std::move(*completed);
                    requests.erase(completed);
                    try {
                        if (req->transfer) {
                            if (req->transferred)
                                req->transferred();
                        } else if (req->type == entry_type::command) {
                            if (req->applied)
                                req->applied(req->status, std::move(req->result));
                        } else if (req->completed)
                            req->completed(req->status);
                    } catch (...) {
                        fail(errc::io_error, "user completion");
                    }
                    continue;
                }
                break;
            }
            if (closing && backend_closed && operations.empty() && !closed) {
                closed = true;
                closing_owner.reset();
                auto callbacks = std::move(close_callbacks);
                for (auto& callback : callbacks) {
                    try {
                        if (callback)
                            callback();
                    } catch (...) {
                        record(errc::io_error, "close completion");
                    }
                }
            }
            draining = false;
        }

        error_code persist(core_update::entries_update update) {
            auto entries = std::allocate_shared<const entry_batch>(
                resource_allocator<entry_batch>{}, std::move(update.batch));
            if (auto status = capture_io(backend->truncate(update.index)))
                return status;
            // The event is admitted; close will settle its client request.
            if (closing)
                return {};
            return launch(
                [&](completion callback) {
                    return backend->append(update.index, entries, std::move(callback));
                },
                [this, entries, index = update.index](error_code status) {
                    if (closing)
                        return;
                    if (status) {
                        fail(status, "io.append completion");
                        return;
                    }
                    if (core.snapshot_installing)
                        return;
                    log_index last = 0;
                    for (size_t i = 0; i < entries->size(); ++i) {
                        if (core.cache.term_of(index + i) != (*entries)[i]->term)
                            break;
                        last = index + i;
                    }
                    if (last > core.last_stored)
                        enqueue(persisted_entries_event{last});
                });
        }

        error_code send(message rpc) {
            auto value =
                std::allocate_shared<message>(resource_allocator<message>{}, std::move(rpc));
            if (auto snapshot = std::get_if<install_snapshot>(&value->payload);
                snapshot && snapshot->needs_data) {
                auto result = std::allocate_shared<optional<raftpp::snapshot>>(
                    resource_allocator<optional<raftpp::snapshot>>{});
                return launch(
                    [&](completion callback) {
                        return backend->snapshot_get(
                            [result, callback = std::move(callback)](
                                error_code status, optional<raftpp::snapshot> snapshot) mutable {
                                *result = std::move(snapshot);
                                callback(status);
                            });
                    },
                    [this, value, result](error_code status) {
                        if (closing || status || !*result)
                            return;
                        auto& payload = std::get<install_snapshot>(value->payload);
                        raftpp::snapshot compacted;
                        if (copy_snapshot(**result, compacted))
                            return;
                        payload.last_index = compacted.index;
                        payload.last_term = compacted.term;
                        payload.configuration_index = compacted.configuration_index;
                        payload.configuration = std::move(compacted.configuration);
                        payload.data = std::move(compacted.buffers[0]);
                        payload.needs_data = false;
                        (void)send(std::move(*value));
                    });
            }
            return launch(
                [&](completion callback) { return backend->send(value, std::move(callback)); },
                [](error_code) {}); // Transport errors are retried by replication.
        }

        error_code put_install(shared<snapshot_job> job) {
            return launch(
                [&](completion callback) {
                    return backend->snapshot_put(0, job->value, std::move(callback));
                },
                [this, job](error_code status) {
                    if (closing)
                        return;
                    if (status) {
                        fail(status, "io.snapshot_put install");
                        return;
                    }
                    restoring = job->value;
                    enqueue(std::move(job->event));
                });
        }

        error_code install(core_update::snapshot_update update) {
            auto job = std::allocate_shared<snapshot_job>(resource_allocator<snapshot_job>{});
            job->value =
                std::allocate_shared<raftpp::snapshot>(resource_allocator<raftpp::snapshot>{});
            auto& value = *job->value;
            value.index = update.metadata.index;
            value.term = update.metadata.term;
            value.configuration = update.metadata.configuration;
            value.configuration_index = update.metadata.configuration_index;
            value.buffers.push_back(std::move(update.chunk));
            job->event =
                persisted_snapshot_event{std::move(update.metadata), update.offset, update.last};
            if (taking_snapshot) {
                pending_install = std::move(job);
                return {};
            }
            return put_install(std::move(job));
        }

        void maybe_snapshot() {
            // Like legacyTakeSnapshot, failure to prepare this optional work
            // must not close a node that has already applied the command.
            try {
                take_snapshot();
            } catch (const bad_alloc&) {
            } catch (const length_error&) {
            }
        }

        void take_snapshot() {
            if (closing || taking_snapshot || core.snapshot_installing ||
                applied < core.commit_index || core.commit_index <= core.trail.snapshot_index() ||
                core.commit_index - core.trail.snapshot_index() < options.snapshot_threshold ||
                core.cache.term_of(core.commit_index) == 0)
                return;
            auto job = std::allocate_shared<snapshot_job>(resource_allocator<snapshot_job>{});
            job->value =
                std::allocate_shared<raftpp::snapshot>(resource_allocator<raftpp::snapshot>{});
            auto& value = *job->value;
            value.index = core.commit_index;
            value.term = core.cache.term_of(value.index);
            value.configuration = core.configuration_committed;
            value.configuration_index = core.configuration_committed_index;
            job->event = snapshot_event{
                {value.index, value.term, value.configuration, value.configuration_index},
                options.snapshot_trailing};
            if (fsm->snapshot(value.buffers))
                return;
            if (closing) {
                fsm->snapshot_finalize(value.buffers);
                return;
            }
            error_code status;
            try {
                status = fsm->snapshot_async(value.buffers);
            } catch (...) {
                status = errc::io_error;
            }
            if (status) {
                fsm->snapshot_finalize(value.buffers);
                return;
            }
            if (closing) {
                fsm->snapshot_finalize(value.buffers);
                return;
            }
            taking_snapshot = true;
            try {
                status = launch(
                    [&](completion callback) {
                        return backend->snapshot_put(options.snapshot_trailing, job->value,
                                                     std::move(callback));
                    },
                    [this, job](error_code completed) {
                        fsm->snapshot_finalize(job->value->buffers);
                        taking_snapshot = false;
                        if (!closing) {
                            if (completed) {
                                fail(completed, "io.snapshot_put local");
                                return;
                            }
                            if (!core.snapshot_installing)
                                enqueue(std::move(job->event));
                            auto pending = std::exchange(pending_install, {});
                            if (pending) {
                                if (auto error = put_install(std::move(pending)))
                                    fail(error, "pending snapshot install");
                            }
                        }
                    });
            } catch (...) {
                taking_snapshot = false;
                fsm->snapshot_finalize(value.buffers);
                throw;
            }
            if (status) {
                taking_snapshot = false;
                fsm->snapshot_finalize(value.buffers);
            }
        }

        error_code apply_committed() {
            if (restoring && core.commit_index == restoring->index) {
                if (auto status = fsm->restore(restoring->buffers[0]))
                    return status;
                if (closing)
                    return {};
                applied = restoring->index;
                restoring.reset();
            }
            while (!closing && applied < core.commit_index) {
                const auto index = applied + 1;
                const auto entry = core.cache.get(index);
                if (entry == nullptr)
                    return {}; // Installing a snapshot.
                shared<void> result;
                if (entry->type == entry_type::command) {
                    if (auto status = fsm->apply(entry->payload.bytes(), result))
                        return status;
                    if (closing)
                        return {};
                } else if (entry->type == entry_type::change) {
                    raftpp::configuration configuration;
                    if (auto status = configuration.decode(entry->payload.bytes()))
                        return status;
                    internal_events.push_back(configuration_event{index, std::move(configuration)});
                }
                applied = index;
                for (const auto& req : requests) {
                    if (!req->transfer && req->index == index && req->type == entry->type)
                        finish(req, {}, result);
                }
            }
            return {};
        }

        void check_requests() {
            auto leader = std::get_if<leader_state>(&core.state);
            for (const auto& req : requests) {
                if (req->finished)
                    continue;
                if (req->transfer) {
                    if (leader != nullptr && leader->transferee == 0)
                        finish(req, {});
                    if (auto follower = std::get_if<follower_state>(&core.state);
                        follower && follower->leader_id != 0)
                        finish(req, {});
                } else if (req->promotee != 0 && leader != nullptr) {
                    const auto i = core.configuration.index_of(req->promotee);
                    if (i == core.configuration.size()) {
                        finish(req, errc::leadership_lost);
                        continue;
                    }
                    const auto status = leader->progress[i].catch_up;
                    if (status == catch_up_status::aborted)
                        finish(req, errc::no_connection);
                    if (status == catch_up_status::finished) {
                        if (leader->transferee != 0) {
                            finish(req, errc::leadership_lost);
                            continue;
                        }
                        auto configuration = changed_role(req->promotee, role::voter);
                        req->promotee = 0;
                        req->index = core.trail.last_index() + 1;
                        internal_events.push_back(submit_event{{encode_change(configuration)}});
                    }
                }
            }
        }

        void notify_step() {
            auto callback = step_observer;
            if (!callback)
                return;
            // Preserve legacyShouldFireStepCb: a queued NOSPACE reply must
            // reach the client before a step observer reacts to leadership loss.
            for (const auto& req : requests)
                if (req->accepted && req->finished && req->type == entry_type::command &&
                    req->status == errc::no_space)
                    return;
            (*callback)();
        }

        error_code process(event_payload payload) noexcept {
            if (closing)
                return errc::shutdown;
            handling = true;
            io_failure = {};
            error_code status;
            try {
                const bool was_leader = std::holds_alternative<leader_state>(core.state);
                const auto now = backend->time();
                const auto capacity = closing ? uint16_t{} : backend->capacity();
                if (!closing)
                    status = step(core, {now, capacity, std::move(payload)});
                if (!status && !closing) {
                    auto update = std::move(core.update);
                    if (was_leader && !std::holds_alternative<leader_state>(core.state))
                        fail_pending(errc::leadership_lost);
                    check_requests();
                    notify_step();
                    if (!closing && (update.flags & update_current_term))
                        status = capture_io(backend->set_term(core.current_term));
                    if (!status && !closing && (update.flags & update_voted_for))
                        status = capture_io(backend->set_vote(core.voted_for));
                    if (!status && !closing && (update.flags & update_entries))
                        status = persist(std::move(update.entries));
                    if (!status && !closing && (update.flags & update_snapshot))
                        status = install(std::move(update.snapshot));
                    if (!status && !closing)
                        for (auto& message : update.messages) {
                            if (closing)
                                break;
                            auto sent = send(std::move(message));
                            if (sent && sent != errc::no_connection) {
                                status = sent;
                                break;
                            }
                        }
                    if (!status && !closing && (update.flags & update_commit_index)) {
                        io_failure = {};
                        status = apply_committed();
                    }
                    if (!status && !closing)
                        maybe_snapshot();
                    if (status)
                        fail(status, "I/O or FSM update");
                }
            } catch (const bad_alloc&) {
                status = errc::out_of_memory;
                fail(status, "event update allocation");
            } catch (const length_error&) {
                status = errc::too_big;
                fail(status, "event update length");
            } catch (...) {
                status = errc::io_error;
                fail(status, "I/O or FSM exception");
            }
            handling = false;
            return status;
        }

        raftpp::configuration changed_role(server_id id, role role) {
            raftpp::configuration configuration;
            for (const auto& server : core.configuration.servers()) {
                if (auto status = configuration.add(server.id, server.address,
                                                    server.id == id ? role : server.role))
                    throw system_error(status);
            }
            return configuration;
        }
        entry encode_change(const raftpp::configuration& configuration) {
            buffer data;
            if (auto status = configuration.encode(data))
                throw system_error(status);
            return {core.current_term, entry_type::change, shared_buffer{std::move(data)}};
        }

        error_code admit(const shared<request>& req, event_payload event) {
            if (closing || !running)
                return errc::shutdown;
            if (handling)
                return errc::busy;
            requests.push_back(req);
            const auto status = process(std::move(event));
            if (status)
                requests.remove(req);
            else
                req->accepted = true;
            pump();
            return status;
        }

        error_code start() {
            if (closing || core.started)
                return errc::invalid;
            io_state loaded;
            if (auto status = capture_io(backend->load(loaded)))
                return status;
            if (closing)
                return errc::shutdown;
            start_event event;
            event.term = loaded.term;
            event.voted_for = loaded.voted_for;
            event.start_index = loaded.start_index;
            event.entries = std::move(loaded.entries);
            if (loaded.snapshot) {
                raftpp::snapshot compacted;
                if (auto status = copy_snapshot(*loaded.snapshot, compacted))
                    return status;
                if (auto status = fsm->restore(compacted.buffers[0]))
                    return status;
                if (closing)
                    return errc::shutdown;
                applied = compacted.index;
                event.metadata = snapshot_metadata{compacted.index, compacted.term,
                                                   std::move(compacted.configuration),
                                                   compacted.configuration_index};
            } else if (event.entries.size() > 1)
                applied = 1;
            if (auto status = process(std::move(event)))
                return status;
            if (closing)
                return errc::shutdown;
            weak<node_impl> observer = shared_from_this();
            handling = true;
            error_code status;
            try {
                status = backend->start(
                    core.heartbeat_timeout,
                    [observer]() noexcept {
                        if (auto self = observer.lock())
                            self->enqueue(timeout_event{});
                    },
                    [observer](message message) noexcept {
                        if (auto self = observer.lock())
                            self->enqueue(receive_event{std::move(message)});
                    });
            } catch (...) {
                handling = false;
                throw;
            }
            handling = false;
            if (status) {
                capture_io(status);
                fail(status, "io.start");
                return status;
            }
            if (closing)
                return errc::shutdown;
            running = true;
            pump();
            return {};
        }
    };

}

#if RAFTPP_WITH_FIXTURE
namespace raftpp::detail {
    core_state& fixture_access::core(node& value) {
        if (!value.impl_)
            throw invalid_argument("fixture node was moved from");
        return value.impl_->core;
    }
}
#endif

namespace raftpp {

    namespace {
        template <typename F>
        error_code guarded(shared<detail::node_impl> impl, F&& action) noexcept {
            if (!impl || impl->closing)
                return errc::shutdown;
            try {
                return action(*impl);
            } catch (const system_error& error) {
                return error.code();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::io_error;
            }
        }
        bool change_pending(const detail::node_impl& impl) {
            return std::any_of(impl.requests.begin(), impl.requests.end(), [](const auto& req) {
                return !req->finished && req->type == entry_type::change;
            });
        }
        error_code change_allowed(detail::node_impl& impl) {
            const auto leader = std::get_if<detail::leader_state>(&impl.core.state);
            if (leader == nullptr || leader->transferee != 0)
                return errc::not_leader;
            if (change_pending(impl) || impl.core.configuration_uncommitted_index != 0 ||
                leader->promotee_id != 0)
                return errc::cannot_change;
            return {};
        }
    }

    node::node(server_id id, string address, shared<io> backend, shared<state_machine> fsm,
               node_options options) {
        if (!backend || !fsm || id == 0 || address.empty() || options.heartbeat_timeout == 0 ||
            options.heartbeat_timeout >= options.election_timeout ||
            options.election_timeout > numeric_limits<uint32_t>::max() / 2 ||
            options.install_snapshot_timeout == 0)
            throw invalid_argument("invalid node options");
        auto impl =
            std::allocate_shared<detail::node_impl>(resource_allocator<detail::node_impl>{});
        impl->backend = std::move(backend);
        impl->fsm = std::move(fsm);
        impl->options = options;
        auto& core = impl->core;
        core.id = id;
        core.address = std::move(address);
        core.election_timeout = options.election_timeout;
        core.heartbeat_timeout = options.heartbeat_timeout;
        core.install_snapshot_timeout = options.install_snapshot_timeout;
        core.pre_vote = options.pre_vote;
        core.max_catch_up_rounds = options.max_catch_up_rounds;
        core.max_catch_up_round_duration = options.max_catch_up_round_duration;
        core.max_inflight_entries = options.max_inflight_entries;
        core.capacity_threshold = options.capacity_threshold;
        core.force_barrier = options.force_barrier;
        core.tracing = options.tracing;
        maybe_enable_tracing(true);
        weak<detail::node_impl> observer = impl;
        impl->backend_close = [observer]() noexcept {
            if (auto self = observer.lock()) {
                self->backend_closed = true;
                self->pump();
            }
        };
        if (auto status = impl->backend->init(id, core.address)) {
            const auto info = impl->backend->error_details();
            throw system_error(status, info.code == status
                                           ? string{wrap_error_info(info, "io.init").text()}
                                           : "io.init");
        }
        core.now = impl->backend->time();
        core.random = impl->backend->random(0, INT_MAX);
        impl_ = std::move(impl);
    }
    node::~node() {
        if (impl_)
            impl_->begin_close();
    }
    node::node(node&& other) noexcept : impl_(std::move(other.impl_)) {}
    node& node::operator=(node&& other) noexcept {
        if (this != &other) {
            if (impl_)
                impl_->begin_close();
            impl_ = std::move(other.impl_);
        }
        return *this;
    }
    error_code node::bootstrap(const raftpp::configuration& configuration) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            if (impl.starting)
                return errc::busy;
            if (impl.core.started)
                return errc::cannot_bootstrap;
            const auto status = impl.capture_io(impl.backend->bootstrap(configuration));
            if (status)
                impl.record(status, "io.bootstrap");
            return status;
        });
    }
    error_code node::recover(const raftpp::configuration& configuration) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            if (impl.starting)
                return errc::busy;
            if (impl.core.started)
                return errc::invalid;
            const auto status = impl.capture_io(impl.backend->recover(configuration));
            if (status)
                impl.record(status, "io.recover");
            return status;
        });
    }
    error_code node::start() noexcept {
        return guarded(impl_, [](auto& impl) -> error_code {
            if (impl.starting)
                return errc::busy;
            if (impl.core.started)
                return errc::invalid;
            impl.starting = true;
            error_code status;
            try {
                status = impl.start();
            } catch (const bad_alloc&) {
                status = errc::out_of_memory;
            } catch (...) {
                status = errc::io_error;
            }
            impl.starting = false;
            if (status && !impl.closing)
                impl.fail(status, "start");
            // io.start may close inline while handling postpones the pump.
            if (impl.closing)
                impl.pump();
            return status;
        });
    }
    error_code node::close(close_callback callback) noexcept {
        auto impl = impl_;
        if (!impl) {
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
            return {};
        }
        if (impl->closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
            return {};
        }
        try {
            if (callback)
                impl->close_callbacks.push_back(std::move(callback));
        } catch (...) {
            impl->record(errc::out_of_memory, "close callback");
            return errc::out_of_memory;
        }
        impl->begin_close();
        impl->pump();
        return {};
    }
    error_code node::apply(shared_buffer command, apply_callback callback) noexcept {
        return guarded(impl_, [&](auto& impl) {
            auto req = std::allocate_shared<detail::node_impl::request>(
                resource_allocator<detail::node_impl::request>{});
            req->index = impl.core.trail.last_index() + 1;
            req->applied = std::move(callback);
            return impl.admit(
                req, detail::submit_event{
                         {{impl.core.current_term, entry_type::command, std::move(command)}}});
        });
    }
    error_code node::barrier(completion callback) noexcept {
        return guarded(impl_, [&](auto& impl) {
            auto req = std::allocate_shared<detail::node_impl::request>(
                resource_allocator<detail::node_impl::request>{});
            req->type = entry_type::barrier;
            req->index = impl.core.trail.last_index() + 1;
            req->completed = std::move(callback);
            return impl.admit(req,
                              detail::submit_event{{{impl.core.current_term, entry_type::barrier,
                                                     shared_buffer{buffer{8}}}}});
        });
    }
    error_code node::add(server_id id, string_view address, completion callback) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            auto next = impl.core.configuration;
            if (auto status = next.add(id, address, role::spare))
                return status;
            if (auto status = change_allowed(impl))
                return status;
            auto req = std::allocate_shared<detail::node_impl::request>(
                resource_allocator<detail::node_impl::request>{});
            req->type = entry_type::change;
            req->index = impl.core.trail.last_index() + 1;
            req->completed = std::move(callback);
            return impl.admit(req, detail::submit_event{{impl.encode_change(next)}});
        });
    }
    error_code node::remove(server_id id, completion callback) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            auto next = impl.core.configuration;
            if (auto status = next.remove(id))
                return status;
            if (auto status = change_allowed(impl))
                return status;
            auto req = std::allocate_shared<detail::node_impl::request>(
                resource_allocator<detail::node_impl::request>{});
            req->type = entry_type::change;
            req->index = impl.core.trail.last_index() + 1;
            req->completed = std::move(callback);
            return impl.admit(req, detail::submit_event{{impl.encode_change(next)}});
        });
    }
    error_code node::assign(server_id id, role role, completion callback) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            const auto leadership = std::get_if<detail::leader_state>(&impl.core.state);
            if (leadership == nullptr || leadership->transferee != 0)
                return errc::not_leader;
            if (role != role::voter && role != role::spare && role != role::standby)
                return errc::bad_role;
            if (auto status = change_allowed(impl))
                return status;
            const auto server = impl.core.configuration.get(id);
            if (server == nullptr)
                return errc::not_found;
            if (server->role == role)
                return errc::bad_role;
            auto req = std::allocate_shared<detail::node_impl::request>(
                resource_allocator<detail::node_impl::request>{});
            req->type = entry_type::change;
            req->completed = std::move(callback);
            auto& leader = std::get<detail::leader_state>(impl.core.state);
            const auto i = impl.core.configuration.index_of(id);
            if (role == role::voter &&
                leader.progress[i].match_index != impl.core.trail.last_index()) {
                req->promotee = id;
                return impl.admit(req, detail::catch_up_event{id});
            }
            req->index = impl.core.trail.last_index() + 1;
            return impl.admit(
                req, detail::submit_event{{impl.encode_change(impl.changed_role(id, role))}});
        });
    }
    error_code node::transfer(server_id id, close_callback callback) noexcept {
        return guarded(impl_, [&](auto& impl) {
            auto req = std::allocate_shared<detail::node_impl::request>(
                resource_allocator<detail::node_impl::request>{});
            req->transfer = true;
            req->transferred = std::move(callback);
            return impl.admit(req, detail::transfer_event{id});
        });
    }

    node_state node::state() const noexcept {
        return !impl_ || impl_->closing ? node_state::unavailable : raftpp::state(impl_->core);
    }
    term node::current_term() const noexcept {
        return impl_ ? impl_->core.current_term : 0;
    }
    server_id node::voted_for() const noexcept {
        return impl_ ? impl_->core.voted_for : 0;
    }
    log_index node::last_index() const noexcept {
        return impl_ ? impl_->core.trail.last_index() : 0;
    }
    log_index node::commit_index() const noexcept {
        return impl_ ? impl_->core.commit_index : 0;
    }
    log_index node::last_applied() const noexcept {
        return impl_ ? impl_->applied : 0;
    }
    uint64_t node::timeout() const noexcept {
        return impl_ && !impl_->closing ? detail::next_timeout(impl_->core) : 0;
    }
    optional<role> node::local_role() const noexcept {
        if (!impl_)
            return {};
        return raftpp::local_role(impl_->core);
    }
    optional<leader_info> node::leader() const {
        if (!impl_ || impl_->closing)
            return {};
        return raftpp::leader(impl_->core);
    }
    raftpp::configuration node::configuration() const {
        return impl_ ? impl_->core.configuration : raftpp::configuration{};
    }
    error_code node::match_index(server_id id, log_index& index) const noexcept {
        if (!impl_ || impl_->closing)
            return errc::shutdown;
        return raftpp::match_index(impl_->core, id, index);
    }
    error_code node::catch_up(server_id id, catch_up_status& status) const noexcept {
        if (!impl_ || impl_->closing)
            return errc::shutdown;
        return raftpp::catch_up(impl_->core, id, status);
    }
    server_id node::transferee() const noexcept {
        if (!impl_ || impl_->closing)
            return 0;
        return raftpp::transferee(impl_->core);
    }
    error_code node::error() const noexcept {
        return impl_ ? impl_->failure : make_error_code(errc::shutdown);
    }
    string node::error_message() const {
        if (!impl_)
            return "node is closed";
        return impl_->failure ? string{impl_->failure_details.text()} : string{};
    }
    raftpp::error_info node::error_details() const noexcept {
        return impl_ ? impl_->failure_details : make_error_info(errc::shutdown, "node is closed");
    }
    void node::seed(uint32_t value) noexcept {
        if (impl_)
            impl_->core.random = value;
    }
    void node::set_tracer(tracer value) noexcept {
        if (impl_) {
            impl_->options.tracing = value;
            impl_->core.tracing = std::move(value);
        }
    }
    error_code node::set_step_callback(step_callback callback) noexcept {
        auto impl = impl_;
        if (!impl)
            return errc::shutdown;
        try {
            shared<const step_callback> next;
            if (callback)
                next = std::allocate_shared<const step_callback>(
                    resource_allocator<step_callback>{}, std::move(callback));
            impl->step_observer = std::move(next);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        } catch (...) {
            return errc::invalid;
        }
    }
    error_code node::set_election_timeout(unsigned value) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            // Preserve the C legacy driver's 150/15 ms dqlite workaround.
            const bool scale = value == 150 && impl.core.heartbeat_timeout == 15;
            if (auto error = raftpp::set_election_timeout(impl.core, scale ? 450 : value))
                return error;
            if (scale)
                impl.core.heartbeat_timeout = 45;
            return error_code{};
        });
    }
    error_code node::set_heartbeat_timeout(unsigned value) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            return raftpp::set_heartbeat_timeout(impl.core, value);
        });
    }
    error_code node::set_install_snapshot_timeout(unsigned value) noexcept {
        return guarded(impl_, [&](auto& impl) -> error_code {
            return raftpp::set_install_snapshot_timeout(impl.core, value);
        });
    }
    void node::set_pre_vote(bool value) noexcept {
        if (impl_)
            impl_->core.pre_vote = value;
    }
    void node::set_snapshot_threshold(unsigned value) noexcept {
        if (impl_)
            impl_->options.snapshot_threshold = value;
    }
    void node::set_snapshot_trailing(unsigned value) noexcept {
        if (impl_)
            impl_->options.snapshot_trailing = value;
    }
    void node::set_max_catch_up_rounds(unsigned value) noexcept {
        if (impl_)
            impl_->core.max_catch_up_rounds = value;
    }
    void node::set_max_catch_up_round_duration(unsigned value) noexcept {
        if (impl_)
            impl_->core.max_catch_up_round_duration = value;
    }
    void node::set_max_inflight_entries(unsigned value) noexcept {
        if (impl_)
            impl_->core.max_inflight_entries = value;
    }
    void node::set_capacity_threshold(uint16_t value) noexcept {
        if (impl_)
            impl_->core.capacity_threshold = value;
    }

}
