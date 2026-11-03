#include <raftpp/core.hpp>
#include <raftpp/random.hpp>
#include <raftpp/version.hpp>

#include <iostream>
#include <source_location>

namespace r = raftpp;

namespace {
    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }

    void test_types() {
        constexpr r::array<r::string_view, 6> states{"", "follower", "candidate", "leader", "", ""};
        constexpr r::array<r::string_view, 6> roles{"stand-by", "voter", "spare", "", "", ""};
        for (unsigned i = 0; i < states.size(); ++i) {
            check(r::state_name(static_cast<r::node_state>(i)) == states[i], "stable state names");
            check(r::role_name(static_cast<r::role>(i)) == roles[i], "stable role names");
        }
        check(r::reference_version == r::array<unsigned, 3>{0, 22, 1} &&
                  r::reference_version_number == 2201,
              "behavioral reference version");
        r::uint32_t first = 7, second = 7;
        for (unsigned i = 0; i < 500; ++i) {
            const auto low = i % 17, high = low + 2 + i * 1000;
            const auto value = r::random_within_range(first, low, high);
            check(value >= low && value <= high &&
                      value == r::random_within_range(second, low, high) && first == second,
                  "bounded reproducible random sequence");
        }
        const auto before = first;
        check(r::random_within_range(first, 42, 42) == 42 && first == before,
              "singleton range preserves seed");
    }

    void test_settings() {
        for (unsigned state = 1; state <= 3; ++state) {
            r::core_state actual;
            actual.random = 7;
            actual.election_timer_start = 123;
            if (state == 2)
                actual.state.emplace<r::candidate_state>();
            if (state == 3)
                actual.state.emplace<r::leader_state>();
            actual.update.flags = r::update_commit_index;
            check(!r::set_election_timeout(actual, 500), "set pure-core election timeout");
            check(actual.election_timeout == 500 && actual.election_timer_start == 123 &&
                      (state == 3 ||
                       (r::next_timeout(actual) >= 623 && r::next_timeout(actual) <= 1123)),
                  "setter samples bounded deadline without resetting start");
            check(actual.update.flags == r::update_commit_index,
                  "setter preserves existing update flags");
            check(!r::set_heartbeat_timeout(actual, 15), "set heartbeat");

            check(!r::set_election_timeout(actual, 150), "set unscaled event timeout");

            check(actual.election_timeout == 150 && actual.heartbeat_timeout == 15 &&
                      actual.election_timer_start == 123,
                  "pure-core 150/15 has no callback-driver scaling");
            check(!r::set_install_snapshot_timeout(actual, 700), "set snapshot deadline");

            check(actual.install_snapshot_timeout == 700, "snapshot timeout is stored");
            const auto seed = actual.random;
            const auto deadline = r::next_timeout(actual);
            check(r::set_election_timeout(actual, 15) == r::errc::invalid &&
                      r::set_election_timeout(actual, r::numeric_limits<unsigned>::max()) ==
                          r::errc::invalid &&
                      r::set_heartbeat_timeout(actual, 0) == r::errc::invalid &&
                      r::set_heartbeat_timeout(actual, 150) == r::errc::invalid &&
                      r::set_install_snapshot_timeout(actual, 0) == r::errc::invalid,
                  "reject invalid timeout bounds");
            check(actual.random == seed && r::next_timeout(actual) == deadline &&
                      actual.install_snapshot_timeout == 700,
                  "invalid settings preserve values and random state");
        }
    }

    void test_core() {
        r::core_update pending;
        r::weak<const r::entry> lifetime;
        {
            r::core_state core;
            core.id = 1;
            core.address = "node-1";
            core.random = 7;
            check(r::step(core, {0, 0, r::timeout_event{}}) == r::errc::invalid,
                  "only start accepted before startup");
            r::configuration members;
            check(!members.add(1, "node-1", r::role::voter), "configure single voter");
            r::buffer encoded;
            check(!members.encode(encoded), "encode bootstrap configuration");
            r::start_event start;
            start.term = 1;
            start.entries.push_back(
                {1, r::entry_type::change, r::shared_buffer{std::move(encoded)}});
            check(!r::step(core, {123, 65535, std::move(start)}) &&
                      std::holds_alternative<r::leader_state>(core.state),
                  "public event API starts and elects single voter");
            const r::array<r::byte, 3> bytes{r::byte{1}, r::byte{2}, r::byte{3}};
            r::submit_event submit;
            submit.entries.push_back(
                {core.current_term, r::entry_type::command, r::shared_buffer::copy(bytes)});
            check(!r::step(core, {124, 65535, std::move(submit)}), "submit public command event");
            check((core.update.flags & r::update_entries) && core.update.entries.index == 2 &&
                      core.update.entries.batch.size() == 1 && core.commit_index == 1,
                  "command waits for durable append");
            pending = std::move(core.update);
            lifetime = pending.entries.batch.front();
            check(!r::step(core, {125, 65535, r::persisted_entries_event{2}}) &&
                      core.commit_index == 2,
                  "durable completion commits command");
            check((core.update.flags & r::update_commit_index) != 0,
                  "driver receives commit notification");
            const auto flags = core.update.flags;
            check(r::step(core, {124, 65535, r::timeout_event{}}) == r::errc::invalid &&
                      core.update.flags == flags,
                  "backward clock rejected before replacing update");
        }
        check(!lifetime.expired() && pending.entries.batch.front()->payload.size() == 3 &&
                  pending.entries.batch.front()->payload.bytes()[2] == r::byte{3},
              "moved update owns data beyond core destruction");
        pending = {};
        check(lifetime.expired(), "last update release destroys retained entry");
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected public API suite");
        const r::string_view suite{argv[1]};
        if (suite == "public_types")
            test_types();
        else if (suite == "public_settings")
            test_settings();
        else if (suite == "public_core")
            test_core();
        else
            throw std::runtime_error("unknown public API suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
