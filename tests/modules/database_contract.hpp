#ifndef RAFTPP_TEST_DATABASE_CONTRACT_HPP
#define RAFTPP_TEST_DATABASE_CONTRACT_HPP
#include "../composition_support.hpp"
#include <filesystem>
#if defined(__unix__)
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace database_testing {
    using namespace composition_testing;
    struct directory {
        fs::path path;
        explicit directory(string_view name) {
            path = fs::temp_directory_path() /
                   ("raftpp-" + string(name) + "-" +
                    std::to_string(chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directory(path);
        }
        ~directory() {
            error_code error;
            fs::remove_all(path, error);
        }
    };
    inline configuration database_members() {
        configuration value;
        check(!value.add(1, "one", role::voter), "configuration");
        return value;
    }
    inline shared<entry_batch> batch(term value, string_view payload) {
        auto result = std::make_shared<entry_batch>();
        result->push_back(
            std::make_shared<entry>(entry{value, entry_type::command, bytes(payload)}));
        return result;
    }
    inline shared<snapshot> image(log_index index, term value) {
        auto result = std::make_shared<snapshot>();
        result->index = index;
        result->term = value;
        result->configuration = database_members();
        result->configuration_index = 1;
        result->buffers = {bytes("snapshot-a"), bytes("snapshot-b")};
        return result;
    }
    template <typename Store> void contracts(const fs::path& path) {
        bool rejected = false;
        try {
            Store invalid_path{fs::path{string("bad\0path", 8)}};
        } catch (const invalid_argument&) {
            rejected = true;
        }
        check(rejected, "reject embedded null path");
        auto store = std::make_shared<Store>(path);
        check(store->set_term(1) == errc::invalid, "uninitialized");
        check(store->init(0, "one") == errc::bad_id, "bad id");
        check(store->init(1, "") == errc::invalid, "empty address");
        check(!store->init(1, "one"), "init");
        check(store->init(1, "one") == errc::busy, "duplicate init");
        {
            Store competing{path};
            check(static_cast<bool>(competing.init(1, "one")), "exclusive database owner");
            competing.close();
        }
        io_state output;
        check(!store->load(output) && output.term == 0 && output.start_index == 1 &&
                  output.entries.empty(),
              "fresh database");
        check(store->recover(database_members()) == errc::invalid, "cannot recover empty database");
        check(!store->bootstrap(database_members()), "bootstrap");
        check(store->bootstrap(database_members()) == errc::cannot_bootstrap, "repeat bootstrap");
        check(!store->set_term(3) && !store->set_vote(2), "term vote");
        check(store->set_term(2) == errc::invalid, "term cannot regress");
        bool completed = false;
        auto first = batch(3, "unaligned");
        check(!store->append(2, first,
                             [&](error_code error) {
                                 check(!error, "append status");
                                 completed = true;
                             }),
              "append");
        check(completed, "inline durable completion");
        auto invalid = batch(3, "must-not-persist");
        invalid->push_back(nullptr);
        completed = false;
        check(store->append(3, invalid, [&](error_code) { completed = true; }) == errc::invalid &&
                  !completed,
              "invalid batch is not accepted");
        check(!store->load(output) && output.entries.size() == 2 && output.voted_for == 2,
              "invalid batch atomicity");
        check(!store->recover(database_members()), "append recovery configuration");
        check(!store->load(output) && output.entries.size() == 3 &&
                  output.entries.back().term == 3 &&
                  output.entries.back().type == entry_type::change,
              "recovery log entry");
        check(!store->truncate(3), "truncate suffix");
        check(!store->append(3, batch(3, "suffix"), {}), "append suffix");
        auto snap = image(2, 3);
        completed = false;
        check(!store->snapshot_put(1, snap,
                                   [&](error_code error) {
                                       check(!error, "snapshot status");
                                       completed = true;
                                   }) &&
                  completed,
              "snapshot publication");
        check(!store->load(output) && output.start_index == 2 && output.entries.size() == 2 &&
                  output.snapshot && text(*output.snapshot) == "snapshot-asnapshot-b",
              "trailing retention");
        check(store->truncate(2) == errc::invalid, "cannot truncate snapshotted history");
        check(!store->set_term(4), "term clears vote");
        store->close();
        store.reset();
        store = std::make_shared<Store>(path);
        check(!store->init(1, "one"), "reopen");
        check(!store->load(output) && output.term == 4 && output.voted_for == 0 &&
                  output.start_index == 2 && output.entries.size() == 2 && output.snapshot &&
                  output.snapshot->index == 2,
              "durable metadata log snapshot");
        // Snapshot installation beyond the old log atomically discards that log.
        check(!store->snapshot_put(0, image(10, 4), {}), "install snapshot");
        check(!store->load(output) && output.start_index == 11 && output.entries.empty(),
              "snapshot installation gap");
        check(!store->append(11, batch(4, "after-install"), {}), "append after installation");
        check(!store->snapshot_put(0, image(10, 4), {}), "repeat compacted snapshot");
        check(!store->load(output) && output.start_index == 11 && output.entries.size() == 1,
              "repeat snapshot retains suffix");
        auto conflict = image(10, 3);
        check(store->snapshot_put(0, conflict, {}) == errc::invalid,
              "conflicting repeated snapshot");
        bool inside = false, closed = false;
        weak<Store> observed = store;
        check(!store->snapshot_get([&](error_code error, optional<snapshot> value) {
            check(!error && value && value->index == 10, "snapshot get");
            inside = true;
            store->close([&] {
                check(!inside, "close outside callback");
                closed = true;
            });
            store.reset();
            inside = false;
        }),
              "snapshot callback reentrant destruction");
        check(closed && observed.expired(), "database drained and released");
        Store closed_store{path};
        closed_store.close();
        check(closed_store.init(1, "one") == errc::shutdown, "closed fresh instance");
    }
    template <typename Store> void limits(const fs::path& path) {
        storage_limits limits;
        limits.max_entry_size = 64;
        limits.max_snapshot_size = 8;
        limits.max_entries = 2;
        Store store{path, limits};
        check(!store.init(1, "one"), "limits init");
        check(!store.bootstrap(database_members()), "limits bootstrap");
        bool called = false;
        check(store.append(2, batch(1, string(65, 'x')), [&](error_code) { called = true; }) ==
                      errc::too_big &&
                  !called,
              "entry bound");
        check(!store.append(2, batch(1, "ok"), {}), "bounded append");
        check(store.append(3, batch(1, "full"), {}) == errc::too_big, "entry count bound");
        check(store.snapshot_put(0, image(2, 1), {}) == errc::too_big, "snapshot bound");
        io_state state;
        check(!store.load(state) && !state.snapshot && state.entries.size() == 2,
              "limits preserve database");
        store.close();
        limits.max_entry_size = 1;
        Store narrow{path, limits};
        check(narrow.init(1, "one") == errc::too_big, "reopen with smaller bound");
        narrow.close();
    }
    template <typename Store> void node_restart(const fs::path& path) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto clock = std::make_shared<manual_timer>();
            auto network = std::make_shared<controlled_transport>();
            auto disk = std::make_shared<Store>(path);
            auto backend =
                make_io({.storage = disk, .timer = clock, .network = network, .random_seed = 9});
            auto fsm = std::make_shared<machine>();
            node_options options;
            options.election_timeout = 100;
            options.heartbeat_timeout = 10;
            options.snapshot_threshold = 1;
            options.snapshot_trailing = 0;
            options.tracing = tracer::noop();
            node instance{1, "one", backend, fsm, options};
            if (!attempt)
                check(!instance.bootstrap(database_members()), "node bootstrap");
            check(!instance.start(), "node start");
            for (int tick = 0; tick < 20 && instance.state() != node_state::leader; ++tick)
                clock->advance();
            if (instance.state() != node_state::leader) {
                std::cerr << "restart=" << attempt
                          << " state=" << static_cast<int>(instance.state())
                          << " error=" << instance.error().message()
                          << " detail=" << instance.error_details().text()
                          << " db=" << disk->error_details().text() << '\n';
            }
            check(instance.state() == node_state::leader, "node leader");
            if (attempt)
                check(fsm->value == "node-data", "node restores FSM");
            bool applied = false;
            check(!instance.apply(bytes("node-data"),
                                  [&](error_code error, shared<void>) {
                                      check(!error, "node apply");
                                      applied = true;
                                  }),
                  "node submit");
            check(applied, "node completion");
            for (int tick = 0; tick < 20; ++tick)
                clock->advance();
            optional<snapshot> persisted;
            check(!disk->snapshot_get([&](error_code error, optional<snapshot> value) {
                check(!error, "node snapshot");
                persisted = std::move(value);
            }) && persisted,
                  "node durable snapshot");
            bool closed = false;
            instance.close([&] { closed = true; });
            check(closed, "node close");
        }
    }
#if defined(__unix__)
    template <typename Store> void crash_restart(const fs::path& path) {
        int descriptors[2];
        check(pipe(descriptors) == 0, "pipe");
        auto child = fork();
        check(child >= 0, "fork");
        if (!child) {
            close(descriptors[0]);
            try {
                Store store{path};
                if (store.init(1, "one") || store.bootstrap(database_members()) ||
                    store.set_term(5) || store.set_vote(1) ||
                    store.append(2, batch(5, "durable-before-kill"), {}) ||
                    store.snapshot_put(1, image(2, 5), {}))
                    _exit(2);
                char ready = 'x';
                if (write(descriptors[1], &ready, 1) != 1)
                    _exit(3);
                for (;;)
                    pause();
            } catch (...) {
                _exit(4);
            }
        }
        close(descriptors[1]);
        char ready = 0;
        pollfd notification{descriptors[0], POLLIN, 0};
        const auto notified = poll(&notification, 1, 10000);
        const auto got = notified > 0 ? read(descriptors[0], &ready, 1) : -1;
        close(descriptors[0]);
        kill(child, SIGKILL);
        int status = 0;
        waitpid(child, &status, 0);
        check(got == 1 && ready == 'x' && WIFSIGNALED(status), "child durable acknowledgement");
        Store store{path};
        check(!store.init(1, "one"), "crash reopen");
        io_state state;
        check(!store.load(state) && state.term == 5 && state.voted_for == 1 && state.snapshot &&
                  state.snapshot->index == 2 && state.start_index == 2 && state.entries.size() == 1,
              "crash recovers acknowledged transaction");
        store.close();
    }
#endif
    template <typename Store> int run(string_view name) {
        try {
            directory root{name};
            contracts<Store>(root.path / "contracts");
            limits<Store>(root.path / "limits");
            node_restart<Store>(root.path / "node");
#if defined(__unix__)
            crash_restart<Store>(root.path / "crash");
#endif
            return 0;
        } catch (const std::exception& error) {
            std::cerr << name << ": " << error.what() << '\n';
            return 1;
        }
    }
}
#endif
