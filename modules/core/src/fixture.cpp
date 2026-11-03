// Independent C++ port of cowsql/raft src/fixture.c; see LICENSE.md.
#include <raftpp/fixture.hpp>
#include <raftpp/detail/random.hpp>
#include "fixture_internal.hpp"
#include <algorithm>

namespace raftpp::detail {
    namespace {
        template <typename F> error_code guarded(F&& action) noexcept {
            try {
                return action();
            } catch (const system_error& error) {
                return error.code();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        void require(error_code error) {
            if (error)
                throw system_error(error);
        }
        size_t kind(fixture_message_type type) {
            auto value = static_cast<size_t>(type);
            if (value == 0 || value > 6)
                throw invalid_argument("invalid fixture message type");
            return value - 1;
        }
        error_code copy_state(const io_state& input, io_state& output, bool loading) {
            io_state value;
            value.term = input.term;
            value.voted_for = input.voted_for;
            value.start_index = input.start_index;
            if (input.snapshot) {
                value.snapshot.emplace();
                if (auto error = copy_snapshot(*input.snapshot, *value.snapshot))
                    return error;
            }
            if (loading && value.snapshot) {
                if (value.snapshot->index == numeric_limits<log_index>::max())
                    return errc::too_big;
                // The C fixture treats all stored entries as the suffix of its
                // current snapshot, including entries injected before it.
                value.start_index = value.snapshot->index + 1;
            }
            if (auto error = copy_entries(input.entries, value.entries))
                return error;
            output = std::move(value);
            return {};
        }
        message copy_message(const message& input) {
            if (input.payload.valueless_by_exception())
                throw invalid_argument("valueless fixture message");
            message value = input;
            if (const auto append = std::get_if<append_entries>(&input.payload)) {
                resource_vector<entry> entries;
                entries.reserve(append->entries.size());
                for (const auto& item : append->entries) {
                    if (!item)
                        throw invalid_argument("null fixture entry");
                    entries.push_back(*item);
                }
                vector<entry> copied;
                require(copy_entries(entries, copied));
                auto& output = std::get<append_entries>(value.payload).entries;
                output.clear();
                output.reserve(copied.size());
                for (auto& item : copied)
                    output.push_back(std::allocate_shared<const entry>(resource_allocator<entry>{},
                                                                       std::move(item)));
            } else if (auto snapshot = std::get_if<install_snapshot>(&value.payload)) {
                if (snapshot->needs_data)
                    throw invalid_argument("unloaded fixture snapshot");
                snapshot->data = shared_buffer::copy(snapshot->data.bytes());
            }
            return value;
        }
    }
    struct append_job {
        log_index index;
        shared<const entry_batch> entries;
        completion callback;
    };
    struct send_job {
        shared<const message> value;
        completion callback;
    };
    struct transmit_job {
        message value;
    };
    struct put_job {
        unsigned trailing;
        shared<const snapshot> value;
        completion callback;
    };
    struct get_job {
        snapshot_callback callback;
    };
    using fixture_job = variant<append_job, send_job, transmit_job, put_job, get_job>;
    struct fixture_request {
        uint64_t due;
        fixture_job job;
    };
    struct fixture_io;
    struct fixture_server {
        bool alive = true;
        shared<fixture_io> backend;
        resource_owned<node> value;
    };
    struct fixture_state {
        array<fixture_server, fixture::max_servers> servers;
        size_t count = 0, leader = 0;
        uint64_t now = 0;
        log_index committed = 0;
        resource_map<log_index, entry> leader_log;
        function<void(fixture&, const fixture_event&)> hook;
        error_code failure;
        bool closed = false, closing = false, stepping = false;
        fixture_server& at(size_t i) {
            if (i >= count)
                throw out_of_range("fixture server index");
            return servers[i];
        }
        void seed_all();
        void observe();
    };
    struct fixture_io final : io {
        weak<fixture_state> owner;
        size_t index = 0;
        server_id id = 0;
        resource_string address;
        io_state disk;
        unsigned interval = 0, election_timeout = 1000, randomized_timeout = 1000;
        unsigned previous_timeout = 0, previous_election = 0;
        uint32_t seed = 0;
        unsigned network_latency = 15, disk_latency = 10;
        uint64_t next_tick = numeric_limits<uint64_t>::max();
        int fault_delay = -1, fault_repeat = -1;
        array<bool, fixture::max_servers> connected{}, saturated{};
        array<bool, 6> drop{};
        array<uint64_t, 6> sent{}, received{};
        resource_list<resource_owned<fixture_request>> requests;
        function<void()> tick;
        function<void(message)> receive;
        bool started = false, closed = false;

        bool fault() noexcept {
            if (fault_delay < 0)
                return false;
            if (fault_delay > 0) {
                --fault_delay;
                return false;
            }
            if (fault_repeat < 0)
                return true;
            if (fault_repeat > 0) {
                --fault_repeat;
                return true;
            }
            --fault_delay;
            return false;
        }
        error_code available() const noexcept {
            auto state = owner.lock();
            return !state || state->closed || closed ? make_error_code(errc::shutdown)
                                                     : error_code{};
        }
        error_code schedule(uint64_t delay, fixture_job job) {
            return guarded([&]() -> error_code {
                if (auto error = available())
                    return error;
                auto state = owner.lock();
                if (delay > numeric_limits<uint64_t>::max() - state->now)
                    return errc::too_big;
                auto request = allocate_owned<fixture_request>(
                    resource_allocator<fixture_request>{}, state->now + delay, std::move(job));
                requests.push_back(std::move(request));
                return {};
            });
        }
        void notify(completion& callback, error_code status) noexcept {
            try {
                if (callback)
                    callback(status);
            } catch (...) {
                if (auto state = owner.lock())
                    state->failure = errc::io_error;
            }
        }
        error_code init(server_id value, string_view endpoint) override {
            return guarded([&]() -> error_code {
                if (auto error = available())
                    return error;
                address = endpoint;
                id = value;
                return {};
            });
        }
        error_code bootstrap(const configuration& conf) override {
            return guarded([&]() -> error_code {
                if (auto error = available())
                    return error;
                if (fault())
                    return errc::io_error;
                if (disk.term != 0 || !disk.entries.empty() || disk.snapshot)
                    return errc::cannot_bootstrap;
                buffer bytes;
                if (auto error = conf.encode(bytes))
                    return error;
                io_state value;
                value.term = 1;
                value.entries.push_back({1, entry_type::change, shared_buffer{std::move(bytes)}});
                disk = std::move(value);
                return {};
            });
        }
        error_code recover(const configuration&) override {
            if (auto error = available())
                return error;
            return errc::io_error;
        }
        error_code load(io_state& output) override {
            if (auto error = available())
                return error;
            if (fault())
                return errc::io_error;
            return copy_state(disk, output, true);
        }
        error_code start(unsigned value, function<void()> timer,
                         function<void(message)> receiver) override {
            if (auto error = available())
                return error;
            if (started || value == 0)
                return errc::invalid;
            if (fault())
                return errc::io_error;
            auto state = owner.lock();
            if (value > numeric_limits<uint64_t>::max() - state->now)
                return errc::too_big;
            interval = value;
            next_tick = state->now + value;
            tick = std::move(timer);
            receive = std::move(receiver);
            started = true;
            return {};
        }
        void close(close_callback callback) noexcept override {
            closed = true;
            started = false;
            tick = {};
            receive = {};
            while (!requests.empty()) {
                auto request = std::move(requests.front());
                requests.pop_front();
                std::visit(
                    [&](auto& job) {
                        using type = remove_cvref_t<decltype(job)>;
                        if constexpr (std::is_same_v<type, get_job>) {
                            try {
                                if (job.callback)
                                    job.callback(errc::canceled, {});
                            } catch (...) {
                                if (auto state = owner.lock())
                                    state->failure = errc::io_error;
                            }
                        } else if constexpr (!std::is_same_v<type, transmit_job>)
                            notify(job.callback, errc::canceled);
                    },
                    request->job);
            }
            try {
                if (callback)
                    callback();
            } catch (...) {
                if (auto state = owner.lock())
                    state->failure = errc::io_error;
            }
        }
        error_code set_term(term value) override {
            if (auto error = available())
                return error;
            if (fault())
                return errc::io_error;
            disk.term = value;
            disk.voted_for = 0;
            return {};
        }
        error_code set_vote(server_id value) override {
            if (auto error = available())
                return error;
            if (fault())
                return errc::io_error;
            disk.voted_for = value;
            return {};
        }
        error_code truncate(log_index value) override {
            if (auto error = available())
                return error;
            if (value == 0)
                return errc::invalid;
            if (value >= disk.start_index && value - disk.start_index >= disk.entries.size())
                return {};
            if (fault())
                return errc::io_error;
            auto keep =
                value <= disk.start_index ? 0 : static_cast<size_t>(value - disk.start_index);
            disk.entries.resize(keep);
            return {};
        }
        error_code append(log_index value, shared<const entry_batch> entries,
                          completion callback) override {
            if (auto error = available())
                return error;
            if (!entries || entries->empty() || value == 0)
                return errc::invalid;
            if (fault())
                return errc::io_error;
            return schedule(disk_latency,
                            append_job{value, std::move(entries), std::move(callback)});
        }
        error_code send(shared<const message> value, completion callback) override {
            if (auto error = available())
                return error;
            if (!value || value->server_id == 0)
                return errc::bad_id;
            if (value->payload.valueless_by_exception())
                return errc::invalid;
            if (fault())
                return errc::io_error;
            return schedule(0, send_job{std::move(value), std::move(callback)});
        }
        error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                completion callback) override {
            if (!value || value->index == numeric_limits<log_index>::max())
                return errc::invalid;
            return schedule(disk_latency, put_job{trailing, std::move(value), std::move(callback)});
        }
        error_code snapshot_get(snapshot_callback callback) override {
            return schedule(disk_latency, get_job{std::move(callback)});
        }
        uint64_t time() const noexcept override {
            auto state = owner.lock();
            return state ? state->now : 0;
        }
        uint32_t random(uint32_t minimum, uint32_t) noexcept override {
            return minimum;
        }
        uint16_t capacity() const noexcept override {
            return 4096;
        }

        void complete(append_job& job) {
            auto result = guarded([&]() -> error_code {
                if (fault())
                    return errc::io_error;
                if (job.index < disk.start_index ||
                    job.index - disk.start_index > disk.entries.size())
                    return errc::io_error;
                vector<entry> values = disk.entries;
                values.resize(static_cast<size_t>(job.index - disk.start_index));
                if (job.entries->size() > values.max_size() - values.size())
                    return errc::too_big;
                values.reserve(values.size() + job.entries->size());
                for (const auto& item : *job.entries) {
                    if (!item)
                        return errc::invalid;
                    entry copied;
                    if (auto error = copy_entry(*item, copied))
                        return error;
                    values.push_back(std::move(copied));
                }
                disk.entries.swap(values);
                return {};
            });
            notify(job.callback, result);
        }
        void complete(send_job& job) {
            auto result = guarded([&]() -> error_code {
                auto state = owner.lock();
                auto peer = job.value->server_id - 1;
                if (!state || peer >= state->count || !connected[peer])
                    return errc::no_connection;
                auto value = copy_message(*job.value);
                if (auto error = schedule(network_latency, transmit_job{std::move(value)}))
                    return error;
                ++sent[job.value->payload.index()];
                return {};
            });
            notify(job.callback, result);
        }
        void complete(transmit_job& job) {
            auto state = owner.lock();
            auto peer = job.value.server_id - 1;
            auto type = job.value.payload.index();
            if (!state || peer >= state->count || !connected[peer] || saturated[peer] ||
                drop[type] || !state->servers[peer].alive)
                return;
            auto target = state->servers[peer].backend;
            if (!target->started || !target->receive)
                return;
            job.value.server_id = id;
            job.value.server_address = address;
            auto callback = target->receive;
            callback(std::move(job.value));
            ++target->received[type];
        }
        void complete(put_job& job) {
            auto result = guarded([&]() -> error_code {
                snapshot copied;
                if (auto error = copy_snapshot(*job.value, copied))
                    return error;
                // C ioFlushSnapshotPut routes zero-trailing snapshots through
                // truncate(1), including its effective-truncate fault tick.
                // Commit the copied snapshot only after that operation succeeds.
                if (job.trailing == 0) {
                    if (auto error = truncate(1))
                        return error;
                }
                disk.snapshot = std::move(copied);
                if (job.trailing == 0)
                    disk.start_index = disk.snapshot->index + 1;
                return {};
            });
            notify(job.callback, result);
        }
        void complete(get_job& job) {
            optional<snapshot> value;
            auto result = guarded([&]() -> error_code {
                if (disk.snapshot) {
                    value.emplace();
                    return copy_snapshot(*disk.snapshot, *value);
                }
                return {};
            });
            try {
                if (job.callback)
                    job.callback(result, result ? optional<snapshot>{} : std::move(value));
            } catch (...) {
                if (auto state = owner.lock())
                    state->failure = errc::io_error;
            }
        }
        void seed_node(node& value) {
            auto& core = fixture_access::core(value);
            if (randomized_timeout < core.election_timeout ||
                randomized_timeout > 2 * core.election_timeout)
                throw invalid_argument("fixture election timeout outside node range");
            if (previous_timeout != randomized_timeout ||
                previous_election != core.election_timeout) {
                seed = core.random;
                for (;;) {
                    auto next = seed;
                    if (random_within_range(next, core.election_timeout,
                                            2 * core.election_timeout) == randomized_timeout)
                        break;
                    seed = next;
                }
                previous_timeout = randomized_timeout;
                previous_election = core.election_timeout;
            }
            value.seed(seed);
        }
    };
    void fixture_state::seed_all() {
        for (size_t i = 0; i < count; ++i)
            servers[i].backend->seed_node(*servers[i].value);
    }

    void fixture_state::observe() {
        const auto previous = leader;
        auto detected = count;
        term highest = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!servers[i].alive || servers[i].value->state() != node_state::leader)
                continue;
            auto& core = fixture_access::core(*servers[i].value);
            for (size_t j = 0; j < count; ++j) {
                if (j != i && servers[j].value->state() == node_state::leader &&
                    servers[j].value->current_term() == core.current_term)
                    throw system_error(errc::corrupt, "fixture election safety");
            }
            if (core.current_term > highest) {
                highest = core.current_term;
                detected = i;
            }
        }
        if (detected == count) {
            leader = count;
            return;
        }
        size_t quorum = 0, acknowledgments = 0;
        bool acknowledged = true;
        for (size_t i = 0; i < count; ++i) {
            auto& core = fixture_access::core(*servers[i].value);
            auto member = core.configuration.get(i + 1);
            if (!member || member->role == role::spare)
                continue;
            ++quorum;
            if (i == detected || !servers[i].alive || servers[detected].backend->saturated[i])
                continue;
            auto follower = std::get_if<follower_state>(&core.state);
            if (core.current_term != highest || !follower || follower->leader_id != detected + 1) {
                acknowledged = false;
                break;
            }
            ++acknowledgments;
        }
        if (!acknowledged || acknowledgments < quorum / 2) {
            leader = count;
            return;
        }
        auto& core = fixture_access::core(*servers[detected].value);
        if (previous == detected)
            for (const auto& [index, value] : leader_log) {
                auto current = core.cache.get(index);
                if (!current) {
                    if (index <= core.cache.snapshot_index())
                        continue;
                    throw system_error(errc::corrupt, "fixture leader deleted entry");
                }
                if (current->term != value.term || current->type != value.type ||
                    !std::ranges::equal(current->payload.bytes(), value.payload.bytes()))
                    throw system_error(errc::corrupt, "fixture leader overwrote entry");
            }
        resource_map<log_index, entry> copied{leader_log.get_allocator()};
        for (auto index = core.cache.first_index(); index != 0 && index <= core.cache.last_index();
             ++index) {
            entry value;
            require(copy_entry(*core.cache.get(index), value));
            copied.emplace(index, std::move(value));
            if (index == numeric_limits<log_index>::max())
                break;
        }
        leader_log.swap(copied);
        leader = detected;
        committed = std::max(committed, core.commit_index);
    }
}

namespace raftpp {
    namespace {
        detail::fixture_state& state(const shared<detail::fixture_state>& value) {
            if (!value)
                throw invalid_argument("moved-from fixture");
            return *value;
        }
        template <typename F>
        error_code change(const shared<detail::fixture_state>& value, F&& action) noexcept {
            if (!value || value->closed)
                return errc::shutdown;
            return detail::guarded([&] { return action(*value); });
        }
        template <typename F>
        error_code change_server(const shared<detail::fixture_state>& value, size_t index,
                                 F&& action) noexcept {
            return change(value, [&](auto& fixture) -> error_code {
                if (index >= fixture.count)
                    return errc::invalid;
                return action(fixture.servers[index]);
            });
        }
        template <typename F>
        error_code change_link(const shared<detail::fixture_state>& value, size_t from, size_t to,
                               F&& action) noexcept {
            return change(value, [&](auto& fixture) -> error_code {
                if (from >= fixture.count || to >= fixture.count || from == to)
                    return errc::invalid;
                return action(*fixture.servers[from].backend, to);
            });
        }
    }
    fixture::fixture()
        : state_(std::allocate_shared<detail::fixture_state>(
              resource_allocator<detail::fixture_state>{})) {}
    fixture::~fixture() {
        close();
    }
    fixture::fixture(fixture&& other) noexcept : state_(std::move(other.state_)) {}
    fixture& fixture::operator=(fixture&& other) noexcept {
        if (this != &other) {
            close();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    error_code fixture::grow(shared<state_machine> machine, node_options options) noexcept {
        return change(state_, [&](auto& cluster) -> error_code {
            if (!machine)
                return errc::invalid;
            if (cluster.stepping || cluster.closing)
                return errc::busy;
            if (cluster.count == max_servers)
                return errc::too_many;
            auto backend =
                std::allocate_shared<detail::fixture_io>(resource_allocator<detail::fixture_io>{});
            auto i = cluster.count;
            backend->owner = state_;
            backend->index = i;
            backend->election_timeout = options.election_timeout;
            backend->randomized_timeout =
                options.election_timeout +
                static_cast<unsigned>(i) * (options.election_timeout / 10);
            auto value =
                allocate_owned<node>(resource_allocator<node>{}, i + 1, std::to_string(i + 1),
                                     backend, std::move(machine), options);
            for (size_t j = 0; j < i; ++j) {
                backend->connected[j] = true;
                cluster.servers[j].backend->connected[i] = true;
            }
            cluster.servers[i].backend = std::move(backend);
            cluster.servers[i].value = std::move(value);
            if (cluster.leader == i)
                cluster.leader = i + 1;
            ++cluster.count;
            return {};
        });
    }
    error_code fixture::configuration(size_t voters, raftpp::configuration& output) const noexcept {
        return change(state_, [&](auto& cluster) -> error_code {
            if (voters == 0 || voters > cluster.count)
                return errc::invalid;
            raftpp::configuration value;
            for (size_t i = 0; i < cluster.count; ++i)
                if (auto error = value.add(i + 1, std::to_string(i + 1),
                                           i < voters ? role::voter : role::standby))
                    return error;
            output = std::move(value);
            return {};
        });
    }
    error_code fixture::bootstrap(const raftpp::configuration& configuration) noexcept {
        return change(state_, [&](auto& cluster) -> error_code {
            if (cluster.count == 0)
                return errc::invalid;
            for (size_t i = 0; i < cluster.count; ++i)
                if (auto error = cluster.servers[i].value->bootstrap(configuration))
                    return error;
            return {};
        });
    }
    error_code fixture::start() noexcept {
        return change(state_, [&](auto& cluster) -> error_code {
            if (cluster.count == 0)
                return errc::invalid;
            cluster.seed_all();
            for (size_t i = 0; i < cluster.count; ++i)
                if (auto error = cluster.servers[i].value->start())
                    return error;
            return {};
        });
    }
    void fixture::close() noexcept {
        auto keep = state_;
        if (!keep || keep->closed || keep->closing)
            return;
        keep->closing = true;
        keep->hook = {};
        // Match C fixture ioFlushAll: finish accepted disk/send requests in
        // server/FIFO order without advancing time, and discard transmits.
        // Re-scan for work submitted by a completion to an earlier server.
        bool pending;
        do {
            pending = false;
            for (size_t i = 0; i < keep->count; ++i) {
                auto backend = keep->servers[i].backend;
                while (!backend->requests.empty()) {
                    pending = true;
                    auto request = std::move(backend->requests.front());
                    backend->requests.pop_front();
                    try {
                        std::visit(
                            [&](auto& job) {
                                if constexpr (!std::is_same_v<remove_cvref_t<decltype(job)>,
                                                              detail::transmit_job>)
                                    backend->complete(job);
                            },
                            request->job);
                    } catch (...) {
                        keep->failure = errc::io_error;
                    }
                }
            }
        } while (pending);
        keep->closed = true;
        for (size_t i = 0; i < keep->count; ++i)
            (void)keep->servers[i].value->close();
        // Closed backend queues have drained callbacks before node/FSM destruction.
        for (size_t i = 0; i < keep->count; ++i) {
            keep->servers[i].value.reset();
            keep->servers[i].backend.reset();
        }
        keep->count = 0;
        keep->leader = 0;
        keep->leader_log.clear();
    }
    size_t fixture::size() const noexcept {
        return state_ ? state_->count : 0;
    }
    uint64_t fixture::time() const noexcept {
        return state_ ? state_->now : 0;
    }
    size_t fixture::leader_index() const noexcept {
        return state_ ? state_->leader : 0;
    }
    log_index fixture::commit_index() const noexcept {
        return state_ ? state_->committed : 0;
    }
    error_code fixture::error() const noexcept {
        return state_ ? state_->failure : error_code{};
    }
    node& fixture::get(size_t index) {
        return *state(state_).at(index).value;
    }
    const node& fixture::get(size_t index) const {
        return *state(state_).at(index).value;
    }
    shared<io> fixture::backend(size_t index) const {
        return state(state_).at(index).backend;
    }
    bool fixture::alive(size_t index) const {
        return state(state_).at(index).alive;
    }
    server_id fixture::voted_for(size_t index) const {
        return state(state_).at(index).backend->disk.voted_for;
    }
    error_code fixture::persisted(size_t index, io_state& output) const noexcept {
        return change_server(state_, index, [&](auto& server) {
            return detail::copy_state(server.backend->disk, output, false);
        });
    }
    optional<fixture_event> fixture::step() {
        auto keep = state_;
        if (!keep || keep->closed || keep->closing)
            return {};
        if (keep->stepping)
            throw invalid_argument("recursive fixture step");
        struct stepping {
            bool& flag;
            explicit stepping(bool& value) : flag(value) {
                flag = true;
            }
            ~stepping() {
                flag = false;
            }
        } active{keep->stepping};
        keep->seed_all();
        auto due = numeric_limits<uint64_t>::max();
        size_t index = keep->count;
        borrowed<detail::fixture_request> selected = nullptr;
        for (size_t i = 0; i < keep->count; ++i) {
            auto& backend = *keep->servers[i].backend;
            // Strict comparisons preserve index order, with ticks before this
            // server's equal-time requests and FIFO order between requests.
            if (backend.started && backend.next_tick < due) {
                due = backend.next_tick;
                index = i;
                selected = nullptr;
            }
            for (const auto& request : backend.requests)
                if (request->due < due) {
                    due = request->due;
                    index = i;
                    selected = request.get();
                }
        }
        if (index == keep->count)
            return {};
        auto backend = keep->servers[index].backend;
        keep->now = due;
        fixture_event event{index, fixture_event_type::tick, due};
        if (selected) {
            auto position = std::ranges::find_if(
                backend->requests, [&](const auto& item) { return item.get() == selected; });
            auto request = std::move(*position);
            backend->requests.erase(position);
            event.type = std::holds_alternative<detail::send_job>(request->job) ||
                                 std::holds_alternative<detail::transmit_job>(request->job)
                             ? fixture_event_type::network
                             : fixture_event_type::disk;
            std::visit([&](auto& job) { backend->complete(job); }, request->job);
        } else {
            if (backend->interval > numeric_limits<uint64_t>::max() - backend->next_tick) {
                backend->started = false;
                backend->next_tick = numeric_limits<uint64_t>::max();
            } else
                backend->next_tick += backend->interval;
            auto tick = backend->tick;
            if (keep->servers[index].alive && tick)
                tick();
        }
        if (!keep->closed) {
            keep->observe();
            auto callback = keep->hook;
            if (callback)
                callback(*this, event);
        }
        return event;
    }
    optional<fixture_event> fixture::step_n(size_t count) {
        if (count == 0)
            throw invalid_argument("fixture step count is zero");
        optional<fixture_event> event;
        for (size_t i = 0; i < count; ++i) {
            event = step();
            if (!event)
                break;
        }
        return event;
    }
    bool fixture::step_until(const function<bool(const fixture&)>& stop, uint64_t max_msecs) {
        if (!stop)
            throw invalid_argument("empty fixture stop predicate");
        const auto start = time();
        while (!stop(*this) && time() - start < max_msecs)
            if (!step())
                return false;
        return time() - start < max_msecs;
    }
    void fixture::step_until_elapsed(uint64_t msecs) {
        (void)step_until([](const fixture&) { return false; }, msecs);
    }
    bool fixture::step_until_has_leader(uint64_t limit) {
        return step_until([](const fixture& value) { return value.leader_index() < value.size(); },
                          limit);
    }
    bool fixture::step_until_has_no_leader(uint64_t limit) {
        return step_until([](const fixture& value) { return value.leader_index() == value.size(); },
                          limit);
    }
    bool fixture::step_until_applied(size_t index, log_index entry, uint64_t limit) {
        if (index > size())
            throw out_of_range("fixture server index");
        return step_until(
            [=](const fixture& value) {
                if (index < value.size())
                    return value.get(index).last_applied() >= entry;
                for (size_t i = 0; i < value.size(); ++i)
                    if (value.get(i).last_applied() < entry)
                        return false;
                return true;
            },
            limit);
    }
    bool fixture::step_until_state_is(size_t index, node_state target, uint64_t limit) {
        (void)get(index);
        return step_until([=](const fixture& value) { return value.get(index).state() == target; },
                          limit);
    }
    bool fixture::step_until_term_is(size_t index, term target, uint64_t limit) {
        (void)get(index);
        return step_until(
            [=](const fixture& value) { return value.get(index).current_term() == target; }, limit);
    }
    bool fixture::step_until_voted_for(size_t index, size_t candidate, uint64_t limit) {
        (void)get(index);
        (void)get(candidate);
        return step_until(
            [=](const fixture& value) { return value.get(index).voted_for() == candidate + 1; },
            limit);
    }
    bool fixture::step_until_delivered(size_t from, size_t to, uint64_t limit) {
        (void)get(from);
        (void)get(to);
        return step_until(
            [=](const fixture& value) {
                for (const auto& request : state(value.state_).at(from).backend->requests) {
                    if (const auto send = std::get_if<detail::send_job>(&request->job);
                        send && send->value->server_id == to + 1)
                        return false;
                    if (const auto transmit = std::get_if<detail::transmit_job>(&request->job);
                        transmit && transmit->value.server_id == to + 1)
                        return false;
                }
                return true;
            },
            limit);
    }
    void fixture::hook(function<void(fixture&, const fixture_event&)> callback) {
        state(state_).hook = std::move(callback);
    }
    error_code fixture::start_elect(size_t index) noexcept {
        return change(state_, [&](auto& cluster) -> error_code {
            if (index >= cluster.count || cluster.leader != cluster.count)
                return errc::invalid;
            for (size_t i = 0; i < cluster.count; ++i)
                if (cluster.servers[i].value->state() != node_state::follower)
                    return errc::invalid;
            auto& target = detail::fixture_access::core(*cluster.servers[index].value);
            auto self = target.configuration.get(index + 1);
            if (!self || self->role != role::voter)
                return errc::bad_role;
            for (size_t i = 0; i < cluster.count; ++i) {
                auto& core = detail::fixture_access::core(*cluster.servers[i].value);
                auto timeout = i == index
                                   ? std::max<uint64_t>(core.election_timeout,
                                                        cluster.now - core.election_timer_start)
                                   : 2 * core.election_timeout;
                if (timeout > numeric_limits<unsigned>::max())
                    return errc::too_big;
                std::get<detail::follower_state>(core.state).randomized_election_timeout =
                    static_cast<unsigned>(timeout);
            }
            return {};
        });
    }
    bool fixture::elect(size_t index, uint64_t limit) {
        detail::require(start_elect(index));
        return step_until_has_leader(limit) && leader_index() == index;
    }
    bool fixture::depose(uint64_t limit) {
        auto keep = state_;
        if (!keep || keep->leader >= keep->count)
            throw invalid_argument("fixture has no stable leader");
        const auto leader = keep->leader;
        array<bool, max_servers> old{};
        for (size_t i = 0; i < keep->count; ++i)
            if (i != leader) {
                auto& core = detail::fixture_access::core(*keep->servers[i].value);
                if (auto follower = std::get_if<detail::follower_state>(&core.state))
                    follower->randomized_election_timeout = 2 * core.election_timeout;
                old[i] = keep->servers[i].backend->drop[1];
                keep->servers[i].backend->drop[1] = true;
            }
        struct restore {
            shared<detail::fixture_state> value;
            array<bool, max_servers>& old;
            size_t leader;
            ~restore() {
                for (size_t i = 0; i < value->count; ++i)
                    if (i != leader)
                        value->servers[i].backend->drop[1] = old[i];
            }
        } restore{keep, old, leader};
        return step_until_has_no_leader(limit);
    }
    error_code fixture::disconnect(size_t from, size_t to) noexcept {
        return change_link(state_, from, to, [](auto& io, auto peer) -> error_code {
            io.connected[peer] = false;
            return {};
        });
    }
    error_code fixture::reconnect(size_t from, size_t to) noexcept {
        return change_link(state_, from, to, [](auto& io, auto peer) -> error_code {
            io.connected[peer] = true;
            return {};
        });
    }
    error_code fixture::saturate(size_t from, size_t to) noexcept {
        return change_link(state_, from, to, [](auto& io, auto peer) -> error_code {
            if (!io.connected[peer])
                return errc::no_connection;
            io.saturated[peer] = true;
            return {};
        });
    }
    error_code fixture::desaturate(size_t from, size_t to) noexcept {
        return change_link(state_, from, to, [](auto& io, auto peer) -> error_code {
            if (!io.connected[peer])
                return errc::no_connection;
            io.saturated[peer] = false;
            return {};
        });
    }
    bool fixture::saturated(size_t from, size_t to) const {
        (void)get(to);
        if (from == to)
            throw invalid_argument("fixture self link");
        return state(state_).at(from).backend->saturated[to];
    }
    error_code fixture::kill(size_t index) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            for (size_t i = 0; i < size(); ++i)
                if (i != index) {
                    server.backend->saturated[i] = true;
                    state_->servers[i].backend->saturated[index] = true;
                }
            server.alive = false;
            return {};
        });
    }
    error_code fixture::revive(size_t index) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            for (size_t i = 0; i < size(); ++i)
                if (i != index && state_->servers[i].alive) {
                    server.backend->saturated[i] = false;
                    state_->servers[i].backend->saturated[index] = false;
                }
            server.alive = true;
            return {};
        });
    }
    error_code fixture::set_randomized_election_timeout(size_t index, unsigned msecs) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            const auto timeout = detail::fixture_access::core(*server.value).election_timeout;
            if (msecs < timeout || msecs > 2 * timeout)
                return errc::invalid;
            server.backend->randomized_timeout = msecs;
            return {};
        });
    }
    error_code fixture::set_network_latency(size_t index, unsigned value) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            server.backend->network_latency = value;
            return {};
        });
    }
    error_code fixture::set_disk_latency(size_t index, unsigned value) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            server.backend->disk_latency = value;
            return {};
        });
    }
    error_code fixture::set_term(size_t index, term value) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            server.backend->disk.term = value;
            return {};
        });
    }
    error_code fixture::set_snapshot(size_t index, const snapshot& value) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            if (value.index == numeric_limits<log_index>::max())
                return errc::too_big;
            snapshot copied;
            if (auto error = copy_snapshot(value, copied))
                return error;
            server.backend->disk.snapshot = std::move(copied);
            if (server.backend->disk.entries.empty())
                server.backend->disk.start_index = value.index + 1;
            return {};
        });
    }
    error_code fixture::add_entry(size_t index, const entry& value) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            entry copied;
            if (auto error = copy_entry(value, copied))
                return error;
            if (server.backend->disk.entries.size() >=
                numeric_limits<log_index>::max() - server.backend->disk.start_index)
                return errc::too_big;
            server.backend->disk.entries.push_back(std::move(copied));
            return {};
        });
    }
    error_code fixture::io_fault(size_t index, int delay, int repeat) noexcept {
        return change_server(state_, index, [&](auto& server) -> error_code {
            server.backend->fault_delay = delay;
            server.backend->fault_repeat = repeat;
            return {};
        });
    }
    uint64_t fixture::n_send(size_t index, fixture_message_type type) const {
        return state(state_).at(index).backend->sent[detail::kind(type)];
    }
    uint64_t fixture::n_recv(size_t index, fixture_message_type type) const {
        return state(state_).at(index).backend->received[detail::kind(type)];
    }
}
