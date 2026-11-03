#include <raftpp.hpp>
#include <raftpp/detail/random.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <source_location>

namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t count) noexcept;
    extern thread_local std::ptrdiff_t remaining;
}

namespace {

    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    r::shared_buffer data(r::string_view text) {
        return r::shared_buffer::copy({reinterpret_cast<const r::byte*>(text.data()), text.size()});
    }
    r::string text(r::view<const r::byte> bytes) {
        return bytes.empty() ? r::string{}
                             : r::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
    r::configuration members(unsigned count = 1) {
        r::configuration value;
        for (unsigned i = 1; i <= count; ++i)
            check(!value.add(i, "node-" + std::to_string(i), r::role::voter), "add member");
        return value;
    }

    void ignore_completion(r::error_code) {}

    struct memory_io : r::io {
        r::io_state disk;
        r::server_id id = 0;
        r::string address;
        r::uint64_t now = 0;
        r::uint32_t random_state = 0;
        unsigned tick_interval = 0;
        bool inline_completion = false, closing = false;
        r::function<void()> tick;
        r::function<void(r::message)> receive;
        r::close_callback closed;
        r::deque<r::function<void()>> work;
        r::vector<r::message> outgoing;
        r::vector<r::string> calls;
        r::error_code append_submit_error, append_completion_error, term_error, load_error;
        r::error_code snapshot_submit_error, snapshot_completion_error;
        bool fail_during_completion = false;
        r::error_info diagnostic;
        bool replace_diagnostic = false;
        r::error_info error_details() const noexcept override {
            return diagnostic;
        }

        r::error_code init(r::server_id value, r::string_view endpoint) override {
            id = value;
            address = endpoint;
            return {};
        }
        r::error_code bootstrap(const r::configuration& configuration) override {
            if (disk.term != 0)
                return r::errc::cannot_bootstrap;
            r::buffer encoded;
            if (auto status = configuration.encode(encoded))
                return status;
            disk.term = 1;
            disk.entries.push_back(
                {1, r::entry_type::change, r::shared_buffer{std::move(encoded)}});
            return {};
        }
        r::error_code recover(const r::configuration& configuration) override {
            r::buffer encoded;
            if (auto status = configuration.encode(encoded))
                return status;
            disk.entries.push_back(
                {disk.term, r::entry_type::change, r::shared_buffer{std::move(encoded)}});
            return {};
        }
        r::error_code load(r::io_state& state) override {
            if (load_error)
                return load_error;
            state = disk;
            return {};
        }
        r::error_code start(unsigned interval, r::function<void()> timer,
                            r::function<void(r::message)> receiver) override {
            tick_interval = interval;
            tick = std::move(timer);
            receive = std::move(receiver);
            return {};
        }
        void close(r::close_callback callback) noexcept override {
            closing = true;
            tick = {};
            receive = {};
            closed = std::move(callback);
            if (work.empty()) {
                auto done = std::move(closed);
                if (done)
                    done();
            }
        }
        void schedule(r::function<void()> job) {
            if (inline_completion)
                job();
            else
                work.push_back(std::move(job));
        }
        void one() {
            if (!work.empty()) {
                auto job = std::move(work.front());
                work.pop_front();
                job();
            }
            if (closing && work.empty() && closed) {
                auto done = std::move(closed);
                done();
            }
        }
        void drain() {
            unsigned count = 0;
            while (!work.empty()) {
                check(++count < 10000, "I/O queue terminates");
                one();
            }
            one();
        }
        r::error_code set_term(r::term term) override {
            calls.push_back("term");
            if (term_error)
                return term_error;
            disk.term = term;
            disk.voted_for = 0;
            return {};
        }
        r::error_code set_vote(r::server_id vote) override {
            calls.push_back("vote");
            disk.voted_for = vote;
            return {};
        }
        r::error_code truncate(r::log_index) override {
            calls.push_back("truncate");
            return {};
        }
        r::error_code append(r::log_index index, r::shared<const r::entry_batch> entries,
                             r::completion callback) override {
            calls.push_back("append");
            if (append_submit_error)
                return append_submit_error;
            schedule([this, index, entries = std::move(entries), callback = std::move(callback)] {
                if (closing) {
                    callback(r::errc::canceled);
                    return;
                }
                if (append_completion_error) {
                    callback(append_completion_error);
                    if (replace_diagnostic)
                        diagnostic =
                            r::make_error_info(r::errc::io_error, "later unrelated failure");
                    return;
                }
                const auto offset = static_cast<r::size_t>(index - disk.start_index);
                if (offset < disk.entries.size())
                    disk.entries.resize(offset);
                for (const auto& entry : *entries)
                    disk.entries.push_back(*entry);
                if (fail_during_completion)
                    allocation_failure::after(0);
                callback({});
                allocation_failure::after(-1);
            });
            return {};
        }
        r::error_code send(r::shared<const r::message> message, r::completion callback) override {
            if (const auto snapshot = std::get_if<r::install_snapshot>(&message->payload))
                check(!snapshot->needs_data, "driver loads snapshot before backend send");
            calls.push_back("send");
            schedule([this, message = std::move(message), callback = std::move(callback)] {
                if (closing) {
                    callback(r::errc::canceled);
                    return;
                }
                r::encoded_message encoded;
                check(!r::encode_message(*message, encoded), "encode node packet");
                auto size = encoded.header.size();
                for (const auto& part : encoded.payload)
                    size += part.size();
                r::buffer packet{size};
                auto cursor = std::copy(encoded.header.bytes().begin(),
                                        encoded.header.bytes().end(), packet.bytes().begin());
                for (const auto& part : encoded.payload) {
                    if (!part.empty())
                        cursor = std::copy(part.bytes().begin(), part.bytes().end(), cursor);
                }
                r::message decoded;
                r::size_t consumed = 0;
                check(!r::decode_message(r::shared_buffer{std::move(packet)}, decoded, consumed) &&
                          consumed == size,
                      "decode node packet");
                decoded.server_id = message->server_id;
                decoded.server_address = message->server_address;
                outgoing.push_back(std::move(decoded));
                callback({});
            });
            return {};
        }
        r::error_code snapshot_put(unsigned trailing, r::shared<const r::snapshot> snapshot,
                                   r::completion callback) override {
            calls.push_back(trailing == 0 ? "install" : "snapshot");
            if (snapshot_submit_error)
                return snapshot_submit_error;
            schedule([this, snapshot = std::move(snapshot), callback = std::move(callback)] {
                if (closing) {
                    callback(r::errc::canceled);
                    return;
                }
                if (snapshot_completion_error) {
                    callback(snapshot_completion_error);
                    return;
                }
                r::snapshot copied;
                const auto status = r::copy_snapshot(*snapshot, copied);
                if (!status)
                    disk.snapshot = std::move(copied);
                callback(status);
            });
            return {};
        }
        r::error_code snapshot_get(r::snapshot_callback callback) override {
            calls.push_back("snapshot_get");
            schedule([this, callback = std::move(callback)] {
                callback(closing ? r::make_error_code(r::errc::canceled) : r::error_code{},
                         disk.snapshot);
            });
            return {};
        }
        r::uint64_t time() const noexcept override {
            return now;
        }
        r::uint32_t random(r::uint32_t low, r::uint32_t high) noexcept override {
            return r::detail::random_within_range(random_state, low, high);
        }
        r::uint16_t capacity() const noexcept override {
            return 65535;
        }
        void deliver(r::message message) {
            if (receive)
                receive(std::move(message));
        }
    };

    struct machine : r::state_machine {
        r::vector<r::string> commands;
        r::string restored;
        unsigned snapshots = 0, finalized = 0;
        r::error_code apply(r::view<const r::byte> command, r::shared<void>& result) override {
            commands.push_back(text(command));
            result = std::make_shared<r::string>(commands.back());
            return {};
        }
        r::error_code restore(r::shared_buffer value) override {
            restored = text(value.bytes());
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>& buffers) override {
            ++snapshots;
            buffers.push_back(data("fsm-state"));
            return {};
        }
        void snapshot_finalize(r::vector<r::shared_buffer>& buffers) noexcept override {
            ++finalized;
            buffers.clear();
        }
    };

    void test_lifetime() {
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        backend->inline_completion = true;
        auto n = std::make_unique<r::node>(1, "node-1", backend, fsm);
        check(n->state() == r::node_state::follower,
              "constructed node preserves raft_init follower state");
        check(!n->bootstrap(members()) && !n->start(), "start single node");
        check(n->state() == r::node_state::leader && n->last_applied() == 1,
              "single voter self-elects");
        unsigned callbacks = 0;
        bool closed = false;
        auto value = std::make_unique<int>(42);
        auto command = r::buffer::copy(data("first").bytes());
        check(!n->apply(
                  std::move(command),
                  [owner = std::move(value), &n, &callbacks, &closed](r::error_code status,
                                                                      r::shared<void> result) {
                      check(!status && *owner == 42 &&
                                *std::static_pointer_cast<r::string>(result) == "first",
                            "owned move-only completion");
                      ++callbacks;
                      check(!n->barrier([&n, &callbacks, &closed](r::error_code barrier_status) {
                          check(!barrier_status && n->last_applied() == 3,
                                "reentrant barrier completes");
                          ++callbacks;
                          check(!n->close([&closed] { closed = true; }), "register close");
                          n.reset();
                      }),
                            "submit during completion");
                  }),
              "inline completion submission");
        check(!n && closed && callbacks == 2 && fsm->commands.size() == 1,
              "callbacks survive handle destruction");

        backend = std::make_shared<memory_io>();
        fsm = std::make_shared<machine>();
        r::weak<r::state_machine> lifetime = fsm;
        n = std::make_unique<r::node>(1, "node-1", backend, fsm);
        check(!n->bootstrap(members()) && !n->start(), "start deferred node");
        bool canceled = false, drained = false;
        check(!n->apply(data("pending"),
                        [&](r::error_code status, r::shared<void>) {
                            canceled = status == r::errc::leadership_lost;
                        }),
              "deferred append accepted");
        check(!n->close([&] { drained = true; }), "close waits for append");
        n.reset();
        fsm.reset();
        check(canceled && !drained && !lifetime.expired(),
              "pending I/O keeps FSM alive after handle destruction");
        backend->drain();
        check(drained && lifetime.expired(), "close completion releases retained context");

        backend = std::make_shared<memory_io>();
        fsm = std::make_shared<machine>();
        backend->inline_completion = true;
        r::node without_callbacks{1, "node-1", backend, fsm};
        check(!without_callbacks.bootstrap(members()) && !without_callbacks.start(),
              "start null callback case");
        void (*empty_apply)(r::error_code, r::shared<void>) = nullptr;
        void (*empty_completion)(r::error_code) = nullptr;
        void (*empty_close)() = nullptr;
        check(!without_callbacks.apply(data("null"), nullptr) &&
                  !without_callbacks.apply(r::buffer{1}, empty_apply) &&
                  !without_callbacks.barrier(empty_completion) &&
                  !without_callbacks.barrier(nullptr) &&
                  !without_callbacks.barrier(ignore_completion),
              "null and typed null callbacks use optional completion semantics");
        check(fsm->commands.size() == 2 && !without_callbacks.close(empty_close) &&
                  !without_callbacks.close(nullptr),
              "typed null callback does not invoke an empty function pointer");
        check(without_callbacks.add(2, "node-2", nullptr) == r::errc::shutdown &&
                  without_callbacks.assign(2, r::role::voter, nullptr) == r::errc::shutdown &&
                  without_callbacks.remove(2, nullptr) == r::errc::shutdown &&
                  without_callbacks.transfer(2, nullptr) == r::errc::shutdown,
              "all request overloads accept null callbacks");
    }

    void test_failure() {
        for (bool immediate : {false, true}) {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && !n.start(), "start failure case");
            if (immediate)
                backend->append_submit_error = r::errc::io_error;
            else
                backend->append_completion_error = r::errc::io_error;
            unsigned callbacks = 0;
            const auto status = n.apply(data("failed"), [&](r::error_code completed,
                                                            r::shared<void>) {
                check(completed == r::errc::leadership_lost, "accepted request fails on I/O loss");
                ++callbacks;
            });
            check(immediate ? status == r::errc::io_error : !status,
                  "immediate and completion failure differ");
            backend->drain();
            check(callbacks == (immediate ? 0U : 1U) && fsm->commands.empty() &&
                      n.state() == r::node_state::unavailable && n.error() == r::errc::io_error,
                  "failure callback and close contract");
        }
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        r::node follower{1, "node-1", backend, fsm};
        check(!follower.bootstrap(members(3)) && !follower.start(), "start follower");
        unsigned callbacks = 0;
        check(follower.apply(data("not leader"),
                             [&](r::error_code, r::shared<void>) { ++callbacks; }) ==
                      r::errc::not_leader &&
                  callbacks == 0,
              "nonleader submission has no callback");
        backend->calls.clear();
        r::request_vote vote;
        vote.term = 2;
        vote.candidate_id = 2;
        vote.last_log_index = 1;
        vote.last_log_term = 1;
        backend->deliver({2, "node-2", vote});
        const auto term = std::find(backend->calls.begin(), backend->calls.end(), "term");
        const auto voted = std::find(backend->calls.begin(), backend->calls.end(), "vote");
        const auto sent = std::find(backend->calls.begin(), backend->calls.end(), "send");
        check(term < voted && voted < sent && sent != backend->calls.end(),
              "persist term and vote before response");
        backend->drain();
        check(std::get<r::request_vote_result>(backend->outgoing[0].payload).vote_granted,
              "vote response delivered");
        check(!follower.close(), "close follower");

        backend = std::make_shared<memory_io>();
        fsm = std::make_shared<machine>();
        r::node persistence_failure{1, "node-1", backend, fsm};
        check(!persistence_failure.bootstrap(members(3)) && !persistence_failure.start(),
              "start term persistence failure");
        backend->term_error = r::errc::io_error;
        backend->calls.clear();
        backend->deliver({2, "node-2", vote});
        backend->drain();
        check(backend->calls == r::vector<r::string>{"term"} && backend->outgoing.empty() &&
                  persistence_failure.state() == r::node_state::unavailable &&
                  persistence_failure.error() == r::errc::io_error,
              "failed term persistence prevents vote recording and response");
    }

    void test_snapshot() {
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        r::node_options options;
        options.snapshot_threshold = 2;
        options.snapshot_trailing = 1;
        r::node n{1, "node-1", backend, fsm, options};
        check(!n.bootstrap(members()) && !n.start(), "start snapshot node");
        bool completed = false;
        check(!n.apply(data("before"),
                       [&](r::error_code status, r::shared<void>) { completed = !status; }),
              "submit before snapshot");
        backend->one();
        check(completed && fsm->snapshots == 1 && fsm->finalized == 0 && !backend->work.empty(),
              "snapshot data frozen until I/O completes");
        r::install_snapshot snapshot;
        snapshot.term = 3;
        snapshot.last_index = 10;
        snapshot.last_term = 2;
        snapshot.configuration = members();
        snapshot.configuration_index = 1;
        snapshot.data = data("installed-state");
        backend->deliver({2, "node-2", snapshot});
        backend->drain();
        check(n.last_applied() == 10 && n.commit_index() == 10 &&
                  fsm->restored == "installed-state" && fsm->finalized == 1 &&
                  backend->disk.snapshot->index == 10,
              "install waits for local snapshot and restores FSM");
        check(!n.close(), "close snapshot node");
        backend->drain();
    }

    void test_snapshot_allocation() {
        struct snapshot_machine : r::state_machine {
            enum class failure { none, length, other };
            r::shared_buffer state = data("frozen-state");
            failure next_failure = failure::none;
            std::ptrdiff_t fail_after = -1;
            unsigned applied = 0, prepared = 0, finalized = 0;
            bool frozen = false, invalid_finalize = false;
            r::error_code apply(r::view<const r::byte>, r::shared<void>&) override {
                ++applied;
                allocation_failure::after(std::exchange(fail_after, -1));
                return {};
            }
            r::error_code snapshot(r::vector<r::shared_buffer>& buffers) override {
                const auto error = std::exchange(next_failure, failure::none);
                if (error == failure::length)
                    throw std::length_error("snapshot preparation length");
                if (error == failure::other)
                    throw std::runtime_error("snapshot preparation failure");
                buffers.push_back(state);
                frozen = true;
                ++prepared;
                return {};
            }
            void snapshot_finalize(r::vector<r::shared_buffer>& buffers) noexcept override {
                invalid_finalize = invalid_finalize || !frozen;
                frozen = false;
                ++finalized;
                buffers.clear();
            }
            r::error_code restore(r::shared_buffer) override {
                return r::errc::io_error;
            }
        };
        unsigned skipped = 0, submitted = 0, finalized_aborts = 0;
        const r::string address(128, 'x'); // Include owned address-copy allocations beyond SSO.
        for (std::ptrdiff_t position = 0; position < 32; ++position) {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<snapshot_machine>();
            r::configuration configuration;
            check(!configuration.add(1, address, r::role::voter),
                  "configure snapshot allocation case");
            r::node n{1, address, backend, fsm};
            check(!n.bootstrap(configuration) && !n.start(), "start snapshot allocation case");
            n.set_snapshot_threshold(2);
            fsm->fail_after = position;
            unsigned callbacks = 0;
            r::error_code completed;
            check(!n.apply(data("committed"),
                           [&](r::error_code status, r::shared<void>) {
                               completed = status;
                               ++callbacks;
                           }),
                  "accept command before local snapshot allocation failure");
            backend->one();
            allocation_failure::after(-1);
            check(n.state() == r::node_state::leader && !n.error(),
                  "optional local snapshot allocation failure preserves the running node");
            check(callbacks == 1 && !completed && fsm->applied == 1 && n.commit_index() == 2 &&
                      n.last_applied() == 2 && backend->disk.entries.size() == 2,
                  "snapshot preparation failure preserves committed command and callback");
            if (backend->work.empty()) {
                ++skipped;
                check(
                    !fsm->frozen && fsm->prepared == fsm->finalized && !backend->disk.snapshot,
                    "aborted preparation finalizes frozen state exactly once without publishing a snapshot");
                if (fsm->prepared != 0)
                    ++finalized_aborts;
            } else {
                ++submitted;
                check(fsm->frozen && fsm->prepared == 1 && fsm->finalized == 0,
                      "accepted snapshot retains frozen buffers until storage completes");
            }
            backend->drain();
            backend->now += 100;
            backend->tick();
            backend->drain();
            check(backend->disk.snapshot && backend->disk.snapshot->index == 2 &&
                      backend->disk.snapshot->configuration_index == 1 &&
                      backend->disk.snapshot->configuration.size() == 1 &&
                      backend->disk.snapshot->configuration.get(1)->address == address &&
                      backend->disk.snapshot->configuration.get(1)->role == r::role::voter &&
                      fsm->prepared == fsm->finalized && !fsm->frozen && !fsm->invalid_finalize &&
                      n.state() == r::node_state::leader && !n.error(),
                  "later event retries snapshot and releases every successful preparation");
            check(!n.apply(data("after"),
                           [&](r::error_code status, r::shared<void>) {
                               check(!status, "command after skipped snapshot succeeds");
                               ++callbacks;
                           }),
                  "submit after snapshot allocation failure");
            backend->drain();
            check(callbacks == 2 && fsm->applied == 2 && n.last_applied() == 3,
                  "node continues applying commands after retry");
            check(!n.close(), "close snapshot allocation case");
            backend->drain();
        }
        check(skipped >= 6 && finalized_aborts != 0 && submitted != 0,
              "allocation sweep covers metadata, frozen-state cleanup, and successful submission");
        for (auto failure : {snapshot_machine::failure::length, snapshot_machine::failure::other}) {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<snapshot_machine>();
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && !n.start(), "start snapshot exception case");
            n.set_snapshot_threshold(2);
            fsm->next_failure = failure;
            unsigned callbacks = 0;
            check(!n.apply(data("committed"),
                           [&](r::error_code status, r::shared<void>) {
                               check(!status,
                                     "preparation exception preserves successful apply result");
                               ++callbacks;
                           }),
                  "accept before snapshot exception");
            backend->drain();
            check(callbacks == 1 && fsm->applied == 1 && fsm->prepared == 0 && fsm->finalized == 0,
                  "failed first phase is not finalized as a successful freeze");
            if (failure == snapshot_machine::failure::length) {
                check(n.state() == r::node_state::leader && !n.error(),
                      "snapshot length failure skips only optional work");
                backend->now += 100;
                backend->tick();
                backend->drain();
                check(backend->disk.snapshot && fsm->prepared == 1 && fsm->finalized == 1,
                      "length failure retries on a later event");
            } else {
                check(n.state() == r::node_state::unavailable && n.error() == r::errc::io_error,
                      "arbitrary FSM exceptions still close the node");
            }
            check(!n.close(), "close snapshot exception case");
            backend->drain();
        }

        std::cout << "snapshot allocation: " << skipped << " skipped (" << finalized_aborts
                  << " after freeze), " << submitted << " submitted\n";
    }

    void test_cluster() {
        r::array<r::shared<memory_io>, 4> backends;
        r::array<r::shared<machine>, 4> fsms;
        r::array<r::owned<r::node>, 4> nodes;
        r::uint64_t clock = 0;
        r::node_options options;
        options.snapshot_threshold = 3;
        options.snapshot_trailing = 1;
        for (unsigned i = 0; i < 3; ++i) {
            backends[i] = std::make_shared<memory_io>();
            fsms[i] = std::make_shared<machine>();
            nodes[i] = std::make_unique<r::node>(i + 1, "node-" + std::to_string(i + 1),
                                                 backends[i], fsms[i], options);
            check(!nodes[i]->bootstrap(members(3)) && !nodes[i]->start(), "start public cluster");
        }
        auto flush = [&] {
            for (unsigned round = 0; round < 1000; ++round) {
                bool progress = false;
                for (auto& backend : backends) {
                    if (!backend)
                        continue;
                    backend->now = ++clock;
                    if (!backend->work.empty()) {
                        backend->drain();
                        progress = true;
                    }
                    auto outgoing = std::move(backend->outgoing);
                    backend->outgoing.clear();
                    for (auto& message : outgoing) {
                        progress = true;
                        const auto target = message.server_id - 1;
                        message.server_id = backend->id;
                        message.server_address = backend->address;
                        if (target < backends.size() && backends[target]) {
                            backends[target]->now = ++clock;
                            backends[target]->deliver(std::move(message));
                        }
                    }
                }
                if (!progress)
                    return;
            }
            check(false, "public cluster reaches quiescence");
        };
        clock = 2001;
        backends[0]->now = clock;
        backends[0]->tick();
        flush();
        check(nodes[0]->state() == r::node_state::leader, "public API elected leader");
        unsigned applied = 0;
        check(!nodes[0]->apply(data("replicated"),
                               [&](r::error_code status, r::shared<void> result) {
                                   check(!status && *std::static_pointer_cast<r::string>(result) ==
                                                        "replicated",
                                         "public apply result");
                                   ++applied;
                               }),
              "submit public command");
        flush();
        backends[0]->now = (clock += 100);
        backends[0]->tick();
        flush();
        check(applied == 1 && fsms[1]->commands.size() == 1 && fsms[2]->commands.size() == 1,
              "committed command reaches all FSMs");
        unsigned changed = 0;
        check(!nodes[0]->add(4, "node-4",
                             [&](r::error_code status) {
                                 check(!status, "add callback");
                                 ++changed;
                             }),
              "add spare");
        flush();
        backends[3] = std::make_shared<memory_io>();
        fsms[3] = std::make_shared<machine>();
        backends[3]->now = clock;
        nodes[3] = std::make_unique<r::node>(4, "node-4", backends[3], fsms[3]);
        check(!nodes[3]->start(), "start pristine joining node");
        check(!nodes[0]->assign(4, r::role::voter,
                                [&](r::error_code status) {
                                    check(!status, "promotion callback");
                                    ++changed;
                                }),
              "promote spare after catch-up");
        flush();
        check(changed == 2 && nodes[0]->configuration().get(4)->role == r::role::voter &&
                  fsms[3]->restored == "fsm-state" &&
                  std::find(backends[0]->calls.begin(), backends[0]->calls.end(), "snapshot_get") !=
                      backends[0]->calls.end(),
              "promotion loads and installs snapshot before configuration commit");
        check(!nodes[0]->remove(4,
                                [&](r::error_code status) {
                                    check(!status, "remove callback");
                                    ++changed;
                                }),
              "remove joined node");
        flush();
        check(changed == 3 && nodes[0]->configuration().get(4) == nullptr,
              "membership callbacks complete");
        bool transferred = false;
        check(!nodes[0]->transfer(2, [&] { transferred = true; }),
              "request public leadership transfer");
        flush();
        check(transferred && nodes[1]->state() == r::node_state::leader &&
                  nodes[0]->leader()->id == 2,
              "transfer callback observes new leader");
        for (unsigned i = 0; i < nodes.size(); ++i) {
            if (nodes[i])
                check(!nodes[i]->close(), "close public cluster node");
            if (backends[i])
                backends[i]->drain();
        }
    }

    struct startup_io : memory_io {
        unsigned loads = 0, starts = 0, closes = 0;
        r::function<void()> during_load, during_start;
        r::error_code load(r::io_state& state) override {
            ++loads;
            auto action = std::move(during_load);
            if (action)
                action();
            return memory_io::load(state);
        }
        r::error_code start(unsigned interval, r::function<void()> timer,
                            r::function<void(r::message)> receiver) override {
            ++starts;
            const auto status = memory_io::start(interval, std::move(timer), std::move(receiver));
            auto action = std::move(during_start);
            if (action)
                action();
            return status;
        }
        void close(r::close_callback callback) noexcept override {
            ++closes;
            memory_io::close(std::move(callback));
        }
    };
    struct startup_machine : machine {
        unsigned restores = 0;
        r::function<void()> during_restore;
        r::error_code restore(r::shared_buffer value) override {
            ++restores;
            auto action = std::move(during_restore);
            if (action)
                action();
            return machine::restore(std::move(value));
        }
    };
    void startup_disk(startup_io& backend) {
        r::snapshot snapshot;
        snapshot.index = 10;
        snapshot.term = 2;
        snapshot.configuration = members(3);
        snapshot.configuration_index = 1;
        snapshot.buffers = {data("restored-state")};
        backend.disk.term = 3;
        backend.disk.snapshot = std::move(snapshot);
        backend.disk.start_index = 11;
    }
    void test_startup_reentrancy() {
        for (unsigned phase = 0; phase < 4; ++phase) {
            auto backend = std::make_shared<startup_io>();
            startup_disk(*backend);
            auto fsm = std::make_shared<startup_machine>();
            auto n = std::make_unique<r::node>(1, "node-1", backend, fsm);
            r::error_code nested, boot, recovery;
            auto reenter = [&] {
                if (phase == 3)
                    n = std::make_unique<r::node>(std::move(*n));
                nested = n->start();
                boot = n->bootstrap(members());
                recovery = n->recover(members());
            };
            if (phase == 0)
                backend->during_load = reenter;
            else if (phase == 2)
                backend->during_start = reenter;
            else
                fsm->during_restore = reenter;
            const auto status = n->start();
            std::cout << "startup reentry phase=" << phase << " outer=" << status.value()
                      << " nested=" << nested.value() << " loads=" << backend->loads
                      << " restores=" << fsm->restores << '\n';
            check(!status && nested == r::errc::busy && boot == r::errc::busy &&
                      recovery == r::errc::busy,
                  "startup rejects reentrant lifecycle admission without failing outer startup");
            check(backend->loads == 1 && backend->starts == 1 && backend->closes == 0 &&
                      fsm->restores == 1 && backend->disk.entries.empty() &&
                      backend->disk.term == 3 && n->current_term() == 3 &&
                      n->commit_index() == 10 && n->last_applied() == 10 && n->last_index() == 10 &&
                      n->configuration().size() == 3 && n->state() == r::node_state::follower &&
                      !n->error(),
                  "reentry preserves persisted state, restoration, and subscription count");
            check(n->start() == r::errc::invalid && backend->loads == 1 && backend->starts == 1,
                  "completed startup still rejects a duplicate with invalid");
            unsigned closed = 0;
            check(!n->close([&] { ++closed; }), "close reentry case");
            backend->drain();
            check(closed == 1 && backend->closes == 1, "reentry case closes once");
        }
        std::cout << "startup reentry: 4 cases\n";
    }
    void test_startup_close() {
        for (unsigned phase = 0; phase < 3; ++phase)
            for (const bool destroy : {false, true}) {
                auto backend = std::make_shared<startup_io>();
                startup_disk(*backend);
                auto fsm = std::make_shared<startup_machine>();
                auto n = std::make_unique<r::node>(1, "node-1", backend, fsm);
                unsigned closed = 0;
                r::log_index applied_at_close = 999;
                auto interrupt = [&] {
                    applied_at_close = n->last_applied();
                    check(!n->close([&] { ++closed; }), "close within startup hook");
                    if (destroy)
                        n.reset();
                };
                if (phase == 0)
                    backend->during_load = interrupt;
                else if (phase == 1)
                    fsm->during_restore = interrupt;
                else
                    backend->during_start = interrupt;
                const auto status = n->start();
                std::cout << "startup close phase=" << phase << " destroy=" << destroy
                          << " status=" << status.value() << " restores=" << fsm->restores
                          << " starts=" << backend->starts << '\n';
                check(status == r::errc::shutdown && closed == 1 && backend->closes == 1 &&
                          backend->loads == 1 && backend->starts == (phase == 2 ? 1U : 0U) &&
                          fsm->restores == (phase == 0 ? 0U : 1U) && !backend->tick &&
                          !backend->receive,
                      "interrupted startup stops later phases and does not restart subscriptions");
                if (n)
                    check(
                        n->state() == r::node_state::unavailable && !n->error() &&
                            n->last_applied() == applied_at_close &&
                            n->start() == r::errc::shutdown,
                        "explicit close preserves final state without recording a fatal startup error");
                backend->drain();
                check(closed == 1, "startup close completion is not repeated");
            }
        std::cout << "startup close: 6 cases\n";
    }
    struct event_io : memory_io {
        bool armed = false;
        r::string_view point;
        mutable r::function<void()> action;
        mutable unsigned hits = 0, calls_after_close = 0;
        unsigned closes = 0;
        void enter() const {
            if (armed && closing)
                ++calls_after_close;
        }
        void fire(r::string_view name) const {
            if (!armed || point != name || !action)
                return;
            ++hits;
            auto callback = std::move(action);
            callback();
        }
        r::uint64_t time() const noexcept override {
            enter();
            const auto value = memory_io::time();
            fire("time");
            return value;
        }
        r::uint16_t capacity() const noexcept override {
            enter();
            const auto value = memory_io::capacity();
            fire("capacity");
            return value;
        }
        r::error_code set_term(r::term value) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            const auto status = memory_io::set_term(value);
            fire("term");
            return status;
        }
        r::error_code set_vote(r::server_id value) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            const auto status = memory_io::set_vote(value);
            fire("vote");
            return status;
        }
        r::error_code truncate(r::log_index value) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            const auto status = memory_io::truncate(value);
            fire("truncate");
            return status;
        }
        r::error_code append(r::log_index index, r::shared<const r::entry_batch> entries,
                             r::completion callback) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            const auto status = memory_io::append(index, std::move(entries), std::move(callback));
            fire("append");
            return status;
        }
        r::error_code send(r::shared<const r::message> message, r::completion callback) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            const auto status = memory_io::send(std::move(message), std::move(callback));
            fire("send");
            return status;
        }
        r::error_code snapshot_put(unsigned trailing, r::shared<const r::snapshot> value,
                                   r::completion callback) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            return memory_io::snapshot_put(trailing, std::move(value), std::move(callback));
        }
        r::error_code snapshot_get(r::snapshot_callback callback) override {
            enter();
            if (closing)
                return r::errc::shutdown;
            return memory_io::snapshot_get(std::move(callback));
        }
        void close(r::close_callback callback) noexcept override {
            ++closes;
            memory_io::close(std::move(callback));
        }
    };
    struct event_machine : machine {
        r::shared<event_io> backend;
        r::function<void()> action;
        bool restore_point = false;
        unsigned calls_after_close = 0, hits = 0;
        r::error_code apply(r::view<const r::byte> command, r::shared<void>& result) override {
            if (backend->closing)
                ++calls_after_close;
            const auto status = machine::apply(command, result);
            if (!restore_point && action) {
                ++hits;
                auto callback = std::move(action);
                callback();
            }
            return status;
        }
        r::error_code restore(r::shared_buffer value) override {
            if (backend->closing)
                ++calls_after_close;
            const auto status = machine::restore(std::move(value));
            if (restore_point && action) {
                ++hits;
                auto callback = std::move(action);
                callback();
            }
            return status;
        }
    };
    void event_close_case(r::string_view point, bool destroy, bool inline_completion) {
        auto backend = std::make_shared<event_io>();
        backend->inline_completion = inline_completion;
        auto fsm = std::make_shared<event_machine>();
        fsm->backend = backend;
        const bool election = point == "term" || point == "vote" || point == "send";
        const bool restore = point == "restore";
        auto n = std::make_unique<r::node>(1, "node-1", backend, fsm);
        check(!n->bootstrap(members(election || restore ? 3 : 1)) && !n->start(),
              "start event-close case");
        r::vector<r::string> sequence;
        r::error_code close_status, request_status;
        unsigned callbacks = 0;
        r::log_index applied_at_close = 999;
        auto interrupt = [&] {
            applied_at_close = n->last_applied();
            close_status = n->close([&] { sequence.push_back("close"); });
            if (destroy)
                n.reset();
        };
        backend->point = point;
        backend->armed = true;
        backend->calls.clear();
        if (point == "apply" || restore) {
            fsm->restore_point = restore;
            fsm->action = interrupt;
        } else
            backend->action = interrupt;
        if (election) {
            backend->now = n->timeout();
            backend->tick();
        } else if (restore) {
            r::install_snapshot incoming;
            incoming.term = 2;
            incoming.last_index = 10;
            incoming.last_term = 1;
            incoming.configuration = members(3);
            incoming.configuration_index = 1;
            incoming.data = data("restored-event");
            backend->deliver({2, "node-2", std::move(incoming)});
        } else {
            request_status = n->apply(data("event"), [&](r::error_code status, r::shared<void>) {
                ++callbacks;
                sequence.push_back("request");
                check(status == r::errc::leadership_lost, "close completes admitted request once");
            });
        }
        backend->drain();
        std::cout << "event close point=" << point << " destroy=" << destroy
                  << " inline=" << inline_completion << " status=" << request_status.value()
                  << " after_close=" << backend->calls_after_close
                  << " applied_before=" << applied_at_close
                  << " applied_after=" << (n ? n->last_applied() : 999) << '\n';
        check(!close_status && backend->hits + fsm->hits == 1 && backend->closes == 1 &&
                  backend->calls_after_close == 0 && fsm->calls_after_close == 0 &&
                  backend->work.empty() && !backend->tick && !backend->receive,
              "close inside event halts subsequent I/O and FSM calls and drains accepted work");
        if (n)
            check(n->state() == r::node_state::unavailable && !n->error() &&
                      n->last_applied() == applied_at_close,
                  "no fatal error or applied update after explicit close");
        if (!election && !restore) {
            check(!request_status && callbacks == 1 &&
                      sequence == r::vector<r::string>{"request", "close"},
                  "admitted client cancellation precedes close completion");
        } else
            check(callbacks == 0 && sequence == r::vector<r::string>{"close"},
                  "event close completes once");
        if (point == "send")
            check(std::ranges::count(backend->calls, "send") == 1,
                  "close after one vote send suppresses the remaining send");
    }
    void test_io_close() {
        for (auto point : {"truncate", "time", "capacity", "term", "vote", "append", "send"})
            for (bool destroy : {false, true})
                for (bool inline_completion : {false, true})
                    event_close_case(point, destroy, inline_completion);
        std::cout << "event I/O close: 28 cases\n";
    }
    void test_fsm_close() {
        for (auto point : {"apply", "restore"})
            for (bool destroy : {false, true})
                for (bool inline_completion : {false, true})
                    event_close_case(point, destroy, inline_completion);
        std::cout << "event FSM close: 8 cases\n";
    }
    void test_step_observer() {
        auto backend = std::make_shared<memory_io>();
        backend->inline_completion = true;
        auto fsm = std::make_shared<machine>();
        r::node n{1, "node-1", backend, fsm};
        unsigned startup = 0;
        check(!n.set_step_callback([&] { ++startup; }), "register before startup");
        check(!n.bootstrap(members()) && !n.start() && startup != 0,
              "successful startup emits steps");
        backend->calls.clear();
        r::vector<r::pair<r::log_index, r::log_index>> observations;
        unsigned callbacks = 0;
        check(!n.set_step_callback([owner = std::make_unique<int>(7), &n, &backend, &observations] {
            check(*owner == 7, "move-only step callback remains owned");
            observations.push_back({n.commit_index(), n.last_applied()});
            if (observations.size() == 1) {
                check(backend->calls.empty(), "step observation precedes truncate/append");
                check(n.apply(data("nested")) == r::errc::busy,
                      "submission from step observer is busy");
            }
        }),
              "register move-only step observer");
        check(!n.apply(data("first"),
                       [&](auto status, auto) {
                           check(!status, "observed command");
                           ++callbacks;
                       }),
              "submit observed command");
        check(callbacks == 1 &&
                  observations == r::vector<r::pair<r::log_index, r::log_index>>{{1, 1}, {2, 1}} &&
                  n.last_applied() == 2,
              "notify after each core step but before FSM application");

        auto token = std::make_shared<int>(42);
        r::weak<int> observer = token;
        unsigned replacement = 0;
        check(!n.set_step_callback([owner = std::move(token), &n, &observer, &replacement] {
            check(!n.set_step_callback([&replacement] { ++replacement; }),
                  "replace active observer");
            check(!observer.expired() && *owner == 42,
                  "active observer survives its own replacement");
        }),
              "register self-replacing observer");
        check(!n.apply(data("second")) && replacement == 1 && observer.expired(),
              "replacement receives later steps and old capture is released");

        bool unexpected = false;
        r::step_callback next = [&] { unexpected = true; };
        allocation_failure::after(0);
        const auto status = n.set_step_callback(std::move(next));
        allocation_failure::after(-1);
        check(status == r::errc::out_of_memory && !n.apply(data("third")) && replacement == 3 &&
                  !unexpected,
              "registration failure preserves previous observer");
        allocation_failure::after(0);
        const auto cleared = n.set_step_callback(nullptr);
        const auto remaining = allocation_failure::remaining;
        allocation_failure::after(-1);
        check(!cleared && remaining == 0 && !n.apply(data("fourth")) && replacement == 3,
              "clear allocates nothing and suppresses later observations");

        r::node moved{std::move(n)};
        check(n.set_step_callback([] {}) == r::errc::shutdown, "moved-from callback registration");
        token = std::make_shared<int>(9);
        observer = token;
        check(!moved.set_step_callback([owner = std::move(token)] { (void)owner; }),
              "retain closed observer capture");
        check(!moved.close() && !observer.expired() && !moved.set_step_callback() &&
                  observer.expired(),
              "clearing a closed node releases captures");

        for (bool destroy : {false, true}) {
            auto io = std::make_shared<memory_io>();
            auto state = std::make_shared<machine>();
            auto live = std::make_unique<r::node>(1, "node-1", io, state);
            check(!live->bootstrap(members()) && !live->start(), "start close-from-step case");
            r::vector<r::string> sequence;
            unsigned observed = 0;
            check(!live->set_step_callback([&] {
                ++observed;
                check(!live->close([&] { sequence.push_back("close"); }), "close from observer");
                if (destroy)
                    live.reset();
            }),
                  "register closing observer");
            check(!live->apply(data("canceled"),
                               [&](auto result, auto) {
                                   check(result == r::errc::leadership_lost,
                                         "observer close cancels admitted client");
                                   sequence.push_back("request");
                               }),
                  "submit close-from-step command");
            io->drain();
            check(observed == 1 && sequence == r::vector<r::string>{"request", "close"} &&
                      state->commands.empty(),
                  "observer close stops I/O/FSM and preserves request/close order");
        }
        {
            auto io = std::make_shared<memory_io>();
            auto state = std::make_shared<machine>();
            r::node failed{1, "node-1", io, state};
            check(!failed.set_step_callback([] { throw std::runtime_error("observer failure"); }) &&
                      !failed.bootstrap(members()) && failed.start() == r::errc::io_error &&
                      failed.error() == r::errc::io_error && io->closing && !io->tick,
                  "observer exception is contained and closes the node");
        }
        {
            struct no_capacity : memory_io {
                r::uint16_t capacity() const noexcept override {
                    return 0;
                }
            };
            auto io = std::make_shared<no_capacity>();
            auto state = std::make_shared<machine>();
            r::node limited{1, "node-1", io, state};
            check(!limited.bootstrap(members()) && !limited.start(), "start capacity case");
            unsigned notifications = 0, completed = 0;
            check(!limited.set_step_callback([&] { ++notifications; }) &&
                      limited.apply(data("rejected"), [&](auto, auto) { ++completed; }) ==
                          r::errc::no_space &&
                      notifications == 0 && completed == 0,
                  "immediate no-space failure emits no successful step");
        }
        std::cout
            << "step observer: timing, replacement, allocation, clearing, move, close, exception, no-space passed\n";
    }
    void test_restore() {
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        backend->disk.term = 3;
        r::snapshot snapshot;
        snapshot.index = 10;
        snapshot.term = 2;
        snapshot.configuration = members(3);
        snapshot.configuration_index = 1;
        snapshot.buffers = {data("restored-"), data("state")};
        backend->disk.snapshot = snapshot;
        backend->disk.start_index = 11;
        backend->disk.entries = {{2, r::entry_type::command, data("durable")}};
        r::node original{1, "node-1", backend, fsm};
        check(!original.start(), "restore snapshot and uncommitted suffix");
        check(fsm->restored == "restored-state" && fsm->commands.empty() &&
                  original.last_applied() == 10 && original.commit_index() == 10 &&
                  original.last_index() == 11,
              "startup compacts snapshot without applying uncommitted suffix");
        check(original.start() == r::errc::invalid && original.state() == r::node_state::follower,
              "duplicate start preserves live node");
        r::node moved{std::move(original)};
        check(original.state() == r::node_state::unavailable &&
                  original.start() == r::errc::shutdown,
              "moved-from node has no live context");
        r::append_entries heartbeat;
        heartbeat.term = 3;
        heartbeat.prev_log_index = 11;
        heartbeat.prev_log_term = 2;
        heartbeat.leader_commit = 11;
        backend->deliver({2, "node-2", heartbeat});
        backend->drain();
        check(moved.last_applied() == 11 && fsm->commands == r::vector<r::string>{"durable"},
              "new leader commits durable suffix after restore and handle move");
        check(!moved.close(), "close restored node");
        backend->drain();

        struct phased_machine : machine {
            r::error_code snapshot_async(r::vector<r::shared_buffer>&) override {
                return r::errc::io_error;
            }
        };
        backend = std::make_shared<memory_io>();
        auto phased = std::make_shared<phased_machine>();
        r::node_options options;
        options.snapshot_threshold = 2;
        r::node phase_failure{1, "node-1", backend, phased, options};
        check(!phase_failure.bootstrap(members()) && !phase_failure.start(),
              "start failed snapshot phase");
        check(!phase_failure.apply(data("snapshot")), "submit failed snapshot phase");
        backend->drain();
        check(phased->snapshots == 1 && phased->finalized == 1 && !backend->disk.snapshot &&
                  phase_failure.state() == r::node_state::leader,
              "second phase failure finalizes without submitting snapshot");
        check(!phase_failure.close(), "close phase failure");

        backend = std::make_shared<memory_io>();
        fsm = std::make_shared<machine>();
        r::node cancellation{1, "node-1", backend, fsm, options};
        check(!cancellation.bootstrap(members()) && !cancellation.start(),
              "start canceled snapshot");
        check(!cancellation.apply(data("snapshot")), "submit canceled snapshot");
        backend->one();
        check(fsm->snapshots == 1 && fsm->finalized == 0,
              "prepared snapshot remains owned during persistence");
        check(!cancellation.close(), "cancel snapshot during persistence");
        backend->drain();
        check(fsm->finalized == 1 && !backend->disk.snapshot,
              "canceled snapshot finalizes exactly once");

        for (bool immediate : {false, true}) {
            backend = std::make_shared<memory_io>();
            fsm = std::make_shared<machine>();
            r::node failed{1, "node-1", backend, fsm, options};
            check(!failed.bootstrap(members()) && !failed.start(),
                  "start failed snapshot persistence");
            if (immediate)
                backend->snapshot_submit_error = r::errc::io_error;
            else
                backend->snapshot_completion_error = r::errc::io_error;
            unsigned callbacks = 0;
            check(!failed.apply(
                      data("snapshot"),
                      [&](r::error_code status, r::shared<void>) {
                          check(!status,
                                "snapshot persistence failure does not undo applied command");
                          ++callbacks;
                      }),
                  "submit snapshot failure case");
            backend->drain();
            check(
                fsm->snapshots == 1 && fsm->finalized == 1 && !backend->disk.snapshot &&
                    callbacks == 1,
                "failed snapshot persistence finalizes once and retains successful command result");
            check(failed.state() ==
                      (immediate ? r::node_state::leader : r::node_state::unavailable),
                  "immediate local snapshot failure skips attempt; completion failure closes node");
            check(!failed.close(), "close snapshot failure case");
            backend->drain();
        }
    }

    void test_errors() {
        for (bool inline_completion : {false, true}) {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            backend->inline_completion = inline_completion;
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && !n.start(), "start diagnostic node");
            backend->append_completion_error = r::errc::io_error;
            backend->diagnostic =
                r::make_error_info(r::errc::io_error, "fsync", "store/segment",
                                   std::make_error_code(std::errc::no_space_on_device));
            backend->replace_diagnostic = true;
            unsigned callbacks = 0;
            check(!n.apply(data("failure"), [&](auto, auto) { ++callbacks; }),
                  "diagnostic request accepted");
            backend->drain();
            check(callbacks == 1 && n.error() == r::errc::io_error &&
                      n.error_details().cause == std::errc::no_space_on_device &&
                      n.error_details().text().find("store/segment") != r::string_view::npos,
                  "completion captures diagnostic before backend overwrite/close");
            check(n.error_message() == n.error_details().text(),
                  "owning message and fixed diagnostic agree");
            allocation_failure::after(0);
            const auto saved = n.error_details();
            const bool no_allocation = allocation_failure::remaining == 0;
            allocation_failure::after(-1);
            check(no_allocation && saved.cause == std::errc::no_space_on_device,
                  "diagnostic query allocates no storage");
            r::node moved{std::move(n)};
            check(n.error_details().code == r::errc::shutdown &&
                      moved.error_details().cause == saved.cause,
                  "diagnostics follow moved node");
        }
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        r::node n{1, "node-1", backend, fsm};
        check(!n.bootstrap(members()), "bootstrap metadata failure case");
        backend->load_error = r::errc::io_error;
        backend->diagnostic =
            r::make_error_info(r::errc::io_error, "metadata.read", "metadata2",
                               std::make_error_code(std::errc::permission_denied));
        check(n.start() == r::errc::io_error &&
                  n.error_details().cause == std::errc::permission_denied &&
                  n.error_message().find("metadata2") != r::string::npos,
              "startup wrappers preserve first failure context");
        backend->drain();

        backend = std::make_shared<memory_io>();
        r::node follower{1, "node-1", backend, fsm};
        check(!follower.bootstrap(members(3)) && !follower.start(),
              "start metadata diagnostic follower");
        backend->term_error = r::errc::io_error;
        backend->diagnostic =
            r::make_error_info(r::errc::io_error, "metadata.sync", "metadata1",
                               std::make_error_code(std::errc::read_only_file_system));
        r::request_vote vote;
        vote.term = 2;
        vote.candidate_id = 2;
        vote.last_log_index = 1;
        vote.last_log_term = 1;
        backend->deliver({2, "node-2", vote});
        backend->drain();
        check(follower.error() == r::errc::io_error &&
                  follower.error_details().cause == std::errc::read_only_file_system &&
                  follower.error_message().find("metadata1") != r::string::npos,
              "event wrappers preserve metadata failure context");
    }

    void test_queries() {
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        r::node n{1, "node-1", backend, fsm};
        r::log_index match = 999;
        auto status = r::catch_up_status::aborted;
        check(n.state() == r::node_state::follower && !n.local_role() && !n.leader() &&
                  n.transferee() == 0,
              "unconfigured node has no member or known leader");
        check(n.match_index(99, match) == r::errc::not_leader && match == 999 &&
                  n.catch_up(99, status) == r::errc::not_leader &&
                  status == r::catch_up_status::aborted,
              "node queries preserve outputs and check leadership first");
        check(!n.bootstrap(members()) && !n.start(), "start query leader");
        const auto saved = n.leader();
        check(saved && saved->id == 1 && saved->address == "node-1" &&
                  n.local_role() == r::role::voter && !n.match_index(1, match) &&
                  match == n.last_index() && !n.catch_up(1, status) &&
                  status == r::catch_up_status::none,
              "node delegates leader, membership and progress queries");
        match = 999;
        status = r::catch_up_status::aborted;
        check(n.match_index(99, match) == r::errc::bad_id && match == 999 &&
                  n.catch_up(99, status) == r::errc::bad_id &&
                  status == r::catch_up_status::aborted,
              "unknown peer keeps output values");
        check(!n.apply(data("query")), "submit query entry");
        backend->drain();
        check(!n.match_index(1, match) && match == n.last_applied(),
              "query observes durable replication progress");
        r::node moved{std::move(n)};
        check(n.state() == r::node_state::unavailable && !n.leader() && !n.local_role() &&
                  n.transferee() == 0 && n.match_index(1, match) == r::errc::shutdown &&
                  n.catch_up(1, status) == r::errc::shutdown,
              "moved node preserves lifecycle guards around core queries");
        check(!moved.close() && !moved.leader() && moved.state() == r::node_state::unavailable &&
                  moved.transferee() == 0,
              "closing node suppresses active leader observations");
        backend->drain();
        check(saved->address == "node-1", "owned node leader result survives close");
        backend = std::make_shared<memory_io>();
        r::node follower{1, "node-1", backend, fsm};
        check(!follower.bootstrap(members(3)) && !follower.start(), "start query follower");
        r::append_entries heartbeat;
        heartbeat.term = 1;
        heartbeat.prev_log_index = 1;
        heartbeat.prev_log_term = 1;
        heartbeat.leader_commit = 1;
        backend->deliver({2, "node-2", heartbeat});
        backend->drain();
        check(follower.leader() && follower.leader()->id == 2 &&
                  follower.leader()->address == "node-2" &&
                  follower.local_role() == r::role::voter && follower.transferee() == 0,
              "follower query exposes the learned leader identity");
        check(!follower.close(), "close query follower");
        backend->drain();
    }

    void test_numeric_limits() {
        constexpr auto maximum_term = r::numeric_limits<r::term>::max();
        constexpr auto maximum_time = r::numeric_limits<r::uint64_t>::max();
        for (bool pre_vote : {false, true}) {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            r::node n{1, "node-1", backend, fsm};
            n.set_pre_vote(pre_vote);
            check(!n.bootstrap(members(3)), "bootstrap exhausted-term callback node");
            backend->disk.term = maximum_term;
            check(!n.start(), "load a valid maximum term in the callback node");
            backend->now = n.timeout();
            auto tick = backend->tick;
            tick();
            backend->drain();
            check(n.error() == r::errc::too_big && n.state() == r::node_state::unavailable &&
                      backend->closing && backend->disk.term == maximum_term &&
                      backend->disk.voted_for == 0 && backend->outgoing.empty() &&
                      backend->calls.empty(),
                  "exhausted election closes without persisting wrapped term/vote or sending RPCs");
        }
        {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()), "bootstrap maximum-term single voter");
            backend->disk.term = maximum_term;
            check(!n.start() && n.state() == r::node_state::leader,
                  "callback single voter self-elects at maximum term");
            unsigned callbacks = 0;
            check(!n.apply(data("maximum-term"),
                           [&](r::error_code status, r::shared<void>) {
                               check(!status, "maximum-term callback command succeeds");
                               ++callbacks;
                           }),
                  "submit maximum-term callback command");
            backend->drain();
            check(callbacks == 1 && n.last_applied() == 2 &&
                      backend->disk.entries.back().term == maximum_term &&
                      fsm->commands == r::vector<r::string>{"maximum-term"},
                  "last term persists and applies exactly once");
            check(!n.close(), "close maximum-term single voter");
            backend->drain();
        }
        {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            backend->now = maximum_time - 1;
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && n.start() == r::errc::too_big &&
                      n.error() == r::errc::too_big && n.state() == r::node_state::unavailable &&
                      backend->closing && !backend->tick && backend->disk.term == 1 &&
                      backend->calls.empty(),
                  "unsupported startup clock closes before I/O start");
            backend->drain();
        }
        {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && !n.start(),
                  "start callback node before clock exhaustion");
            unsigned callbacks = 0;
            check(!n.apply(data("pending-clock"),
                           [&](r::error_code status, r::shared<void>) {
                               check(status == r::errc::leadership_lost,
                                     "clock exhaustion fails the accepted request");
                               ++callbacks;
                           }),
                  "accept command before clock exhaustion");
            backend->now = maximum_time - 1;
            auto tick = backend->tick;
            tick();
            backend->drain();
            check(
                n.error() == r::errc::too_big && n.state() == r::node_state::unavailable &&
                    callbacks == 1 && backend->disk.entries.size() == 1 && fsm->commands.empty() &&
                    backend->work.empty(),
                "clock exhaustion closes, drains queued I/O and completes the accepted request once");
        }
        {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            backend->now = maximum_time - 2000;
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && !n.start(), "start callback leader at clock horizon");
            const auto deadline = n.timeout();
            check(n.set_election_timeout(2000) == r::errc::too_big && n.timeout() == deadline &&
                      !n.error() && !backend->closing && n.state() == r::node_state::leader,
                  "rejected callback timeout growth preserves a running node");
            check(!n.set_election_timeout(500) && n.timeout() == maximum_time - 1500,
                  "callback timeout decrease retains representable deadline");
            check(!n.close(), "close clock-horizon callback node");
            backend->drain();
        }
        std::cout << "node numeric limits: 6 cases\n";
    }

    void test_settings() {
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<machine>();
        backend->now = 123;
        r::node n{1, "node-1", backend, fsm};
        n.seed(7);
        check(n.timeout() == 0, "unstarted timer has no randomized duration");
        check(!n.set_heartbeat_timeout(15) && !n.set_election_timeout(150),
              "set legacy callback-driver timeouts");
        check(n.timeout() >= 450 && n.timeout() <= 900, "legacy 150/15 scales to 450/45");
        check(!n.bootstrap(members()) && !n.start(), "start configured single voter");
        check(backend->tick_interval == 45 && n.timeout() == 573,
              "scaled heartbeat reaches I/O and absolute quorum timeout");
        check(!n.set_election_timeout(500) && n.timeout() == 623,
              "leader setter preserves timer start");
        check(!n.set_heartbeat_timeout(50) && !n.set_install_snapshot_timeout(700),
              "runtime timeout settings accepted");
        check(n.set_election_timeout(50) == r::errc::invalid &&
                  n.set_heartbeat_timeout(500) == r::errc::invalid &&
                  n.set_install_snapshot_timeout(0) == r::errc::invalid && n.timeout() == 623,
              "invalid node settings preserve deadline");
        check(r::state_name(n.state()) == "leader" && r::role_name(*n.local_role()) == "voter",
              "public query names");
        r::node moved{std::move(n)};
        check(n.timeout() == 0 && moved.timeout() == 623, "deadline follows moved node handle");
        check(!moved.close() && moved.timeout() == 0, "closing has no deadline");
        backend->drain();

        backend = std::make_shared<memory_io>();
        r::node_options options;
        options.heartbeat_timeout = 15;
        options.election_timeout = 150;
        r::node explicit_options{1, "node-1", backend, fsm, options};
        check(!explicit_options.bootstrap(members()) && !explicit_options.start(),
              "start with explicit constructor options");
        check(backend->tick_interval == 15 && explicit_options.timeout() == 150,
              "constructor options remain explicit and unscaled");
        check(!explicit_options.close(), "close explicit-options node");
        backend->drain();
    }

    void test_allocation() {
        unsigned failed = 0, accepted = 0;
        for (std::ptrdiff_t limit = 0; limit < 80; ++limit) {
            auto backend = std::make_shared<memory_io>();
            auto fsm = std::make_shared<machine>();
            r::node n{1, "node-1", backend, fsm};
            check(!n.bootstrap(members()) && !n.start(), "start allocation case");
            unsigned callbacks = 0;
            auto payload = data("allocation");
            r::apply_callback callback = [&](r::error_code, r::shared<void>) { ++callbacks; };
            allocation_failure::after(limit);
            const auto status = n.apply(payload, std::move(callback));
            allocation_failure::after(-1);
            if (status) {
                ++failed;
                check(status == r::errc::out_of_memory && callbacks == 0,
                      "immediate allocation failure has no callback");
            } else
                ++accepted;
            backend->drain();
            check(callbacks == (status ? 0U : 1U),
                  "accepted request completes exactly once after failure injection");
            check(!n.close(), "close allocation case");
            backend->drain();
        }
        check(failed > 5 && accepted != 0,
              "allocation sweep covers request setup and I/O submission");
        struct counting_machine : machine {
            unsigned count = 0;
            r::error_code apply(r::view<const r::byte>, r::shared<void>&) override {
                ++count;
                return {};
            }
        };
        auto backend = std::make_shared<memory_io>();
        auto fsm = std::make_shared<counting_machine>();
        r::node n{1, "node-1", backend, fsm};
        check(!n.bootstrap(members()) && !n.start(), "start completion allocation case");
        bool done = false;
        check(!n.apply(data("completion"),
                       [&](r::error_code status, r::shared<void>) { done = !status; }),
              "accept completion case");
        backend->fail_during_completion = true;
        backend->drain();
        check(done && fsm->count == 1 && n.last_applied() == 2,
              "completion bookkeeping needs no allocation for this request");
        r::close_callback closed = [] {};
        allocation_failure::after(0);
        const auto status = n.close(std::move(closed));
        allocation_failure::after(-1);
        check(status == r::errc::out_of_memory && n.state() == r::node_state::leader,
              "close callback registration failure is reported before closing");
        check(!n.close(), "close without allocation");
    }

}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected node suite");
        const r::string_view suite{argv[1]};
        if (suite == "node_lifetime")
            test_lifetime();
        else if (suite == "node_failure")
            test_failure();
        else if (suite == "node_snapshot")
            test_snapshot();
        else if (suite == "node_snapshot_allocation")
            test_snapshot_allocation();
        else if (suite == "node_cluster")
            test_cluster();
        else if (suite == "node_restore")
            test_restore();
        else if (suite == "node_startup_reentrancy")
            test_startup_reentrancy();
        else if (suite == "node_startup_close")
            test_startup_close();
        else if (suite == "node_io_close")
            test_io_close();
        else if (suite == "node_fsm_close")
            test_fsm_close();
        else if (suite == "node_step_observer")
            test_step_observer();
        else if (suite == "node_allocation")
            test_allocation();
        else if (suite == "node_settings")
            test_settings();
        else if (suite == "node_numeric_limits")
            test_numeric_limits();
        else if (suite == "node_queries")
            test_queries();
        else if (suite == "node_errors")
            test_errors();
        else
            throw std::runtime_error("unknown node suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
