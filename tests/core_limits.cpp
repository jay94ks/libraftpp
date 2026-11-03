#include <raftpp/core.hpp>

#include <iostream>
#include <source_location>

namespace r = raftpp;
namespace {
    constexpr auto maximum = r::numeric_limits<r::log_index>::max();
    void check(bool value, r::string_view message,
               r::source_location where = r::source_location::current()) {
        if (!value)
            throw r::system_error(r::make_error_code(r::errc::invalid),
                                  r::string{where.file_name()} + ":" +
                                      std::to_string(where.line()) + ": " + r::string{message});
    }
    r::configuration members() {
        r::configuration value;
        for (unsigned id = 1; id <= 3; ++id)
            check(!value.add(id, "node-" + std::to_string(id), r::role::voter),
                  "configure boundary core");
        return value;
    }
    r::start_event starting(r::log_index index) {
        r::start_event value;
        value.term = 3;
        value.metadata = r::snapshot_metadata{index, 2, members(), 1};
        value.start_index = index == maximum ? maximum : index + 1;
        return value;
    }
    r::entry command(r::term term) {
        return {term, r::entry_type::command, {}};
    }
    r::error_code event(r::core_state& core, r::event_payload value) {
        return r::step(core, {core.now + 1, 8192, std::move(value)});
    }
    void initialize(r::core_state& core, r::log_index index) {
        core.id = 1;
        core.address = "node-1";
        core.random = 1729;
        check(!event(core, starting(index)), "start at high snapshot index");
    }
    unsigned submission() {
        r::core_state core;
        initialize(core, maximum - 2);
        check(!r::step(core, {r::next_timeout(core), 8192, r::timeout_event{}}),
              "elect boundary candidate");
        r::request_vote_result vote;
        vote.term = core.current_term;
        vote.vote_granted = true;
        check(!event(core, r::receive_event{{2, "node-2", vote}}) &&
                  r::state(core) == r::node_state::leader,
              "elect boundary leader");
        r::append_entries_result ack;
        ack.term = core.current_term;
        ack.last_log_index = maximum - 2;
        check(!event(core, r::receive_event{{2, "node-2", ack}}),
              "peer enters pipeline near index limit");
        const auto status =
            event(core, r::submit_event{{command(core.current_term), command(core.current_term)}});
        if (status != r::errc::too_big) {
            const auto& progress = std::get<r::leader_state>(core.state).progress[1];
            std::cerr << "terminal submission status=" << status.value()
                      << " last=" << core.trail.last_index() << " next=" << progress.next_index
                      << '\n';
        }
        check(status == r::errc::too_big && core.trail.last_index() == maximum - 2 &&
                  core.cache.empty() && core.commit_index == maximum - 2 &&
                  !(core.update.flags & r::update_entries) && core.update.messages.empty(),
              "terminal index must be rejected before cache/pipeline mutation");
        check(!event(core, r::submit_event{{command(core.current_term)}}) &&
                  core.update.entries.index == maximum - 1 &&
                  core.update.entries.batch.size() == 1 &&
                  std::get<r::leader_state>(core.state).progress[1].next_index == maximum,
              "largest usable index retains a representable next index");
        check(!event(core, r::persisted_entries_event{maximum - 1}),
              "persist largest usable index");
        ack.last_log_index = maximum;
        check(!event(core, r::receive_event{{2, "node-2", ack}}) &&
                  core.commit_index == maximum - 1 &&
                  std::get<r::leader_state>(core.state).progress[1].match_index == maximum - 1 &&
                  std::get<r::leader_state>(core.state).progress[1].next_index == maximum,
              "oversized remote ACK clamps safely to the local boundary");
        check(event(core, r::submit_event{{command(core.current_term)}}) == r::errc::too_big &&
                  core.trail.last_index() == maximum - 1 && core.cache.size() == 1 &&
                  core.commit_index == maximum - 1 && r::state(core) == r::node_state::leader,
              "exhausted index space preserves committed state");
        check(!r::step(core, {core.now + core.heartbeat_timeout, 8192, r::timeout_event{}}) &&
                  std::get<r::leader_state>(core.state).progress[1].next_index == maximum,
              "heartbeats at the largest usable index do not wrap pipeline progress");
        return 1;
    }
    unsigned startup() {
        for (const unsigned count : {0, 1, 2}) {
            r::core_state core;
            core.id = 1;
            const bool terminal_snapshot = count == 0;
            auto start = starting(terminal_snapshot ? maximum : maximum - 1);
            for (unsigned i = 0; i < count; ++i)
                start.entries.push_back(command(3));
            check(event(core, std::move(start)) == r::errc::too_big && !core.started &&
                      core.current_term == 0 && core.configuration.empty() && core.cache.empty() &&
                      core.trail.snapshot_index() == 0,
                  "terminal persisted metadata/suffix is rejected before restoring core state");
        }
        for (bool suffix : {false, true}) {
            r::core_state core;
            core.id = 1;
            auto start = starting(suffix ? maximum - 2 : maximum - 1);
            if (suffix)
                start.entries.push_back(command(3));
            check(!event(core, std::move(start)) && core.trail.last_index() == maximum - 1 &&
                      core.cache.last_index() == maximum - 1 && core.last_stored == maximum - 1,
                  "largest valid persisted snapshot/suffix starts without overflow");
        }
        return 5;
    }
    unsigned receiving_entries() {
        r::core_state core;
        initialize(core, maximum - 2);
        r::append_entries append;
        append.term = 3;
        append.prev_log_index = maximum - 2;
        append.prev_log_term = 2;
        append.leader_commit = maximum - 2;
        append.entries.push_back(std::make_shared<const r::entry>(command(3)));
        append.entries.push_back(std::make_shared<const r::entry>(command(3)));
        check(event(core, r::receive_event{{2, "node-2", append}}) == r::errc::malformed &&
                  core.cache.empty() && core.trail.last_index() == maximum - 2 &&
                  !(core.update.flags & r::update_entries),
              "terminal remote append leaves the durable prefix unchanged");
        append.entries.pop_back();
        check(!event(core, r::receive_event{{2, "node-2", append}}) &&
                  core.update.entries.index == maximum - 1 &&
                  core.update.entries.batch.size() == 1 &&
                  !event(core, r::persisted_entries_event{maximum - 1}),
              "remote append at largest usable index can be persisted");
        append.entries.clear();
        append.prev_log_index = maximum - 1;
        append.prev_log_term = 3;
        append.leader_commit = maximum - 1;
        check(!event(core, r::receive_event{{2, "node-2", append}}) &&
                  core.commit_index == maximum - 1 &&
                  std::get<r::append_entries_result>(core.update.messages.front().payload)
                          .last_log_index == maximum - 1,
              "heartbeat acknowledges the largest durable prefix");
        auto invalid_heartbeat = append;
        invalid_heartbeat.prev_log_index = maximum;
        check(event(core, r::receive_event{{2, "node-2", invalid_heartbeat}}) ==
                      r::errc::malformed &&
                  core.commit_index == maximum - 1 && core.last_stored == maximum - 1,
              "heartbeat cannot reference the reserved terminal index");
        append.entries.push_back(std::make_shared<const r::entry>(command(3)));
        check(event(core, r::receive_event{{2, "node-2", append}}) == r::errc::malformed &&
                  core.commit_index == maximum - 1 && core.last_stored == maximum - 1 &&
                  core.cache.size() == 1,
              "terminal append after catch-up preserves committed state");
        return 1;
    }
    unsigned receiving_snapshot() {
        r::core_state core;
        initialize(core, 8);
        r::install_snapshot snapshot;
        snapshot.term = 3;
        snapshot.last_index = maximum;
        snapshot.last_term = 3;
        snapshot.configuration = members();
        snapshot.configuration_index = 1;
        check(event(core, r::receive_event{{2, "node-2", snapshot}}) == r::errc::malformed &&
                  !core.snapshot_installing && core.commit_index == 8 && core.last_stored == 8 &&
                  core.trail.snapshot_index() == 8 && !(core.update.flags & r::update_snapshot),
              "terminal snapshot is rejected before replacing the existing prefix");
        snapshot.last_index = maximum - 1;
        check(!event(core, r::receive_event{{2, "node-2", snapshot}}) && core.snapshot_installing &&
                  (core.update.flags & r::update_snapshot),
              "largest usable remote snapshot can be submitted");
        const auto metadata = core.update.snapshot.metadata;
        check(!event(core, r::persisted_snapshot_event{metadata, 0, true}) &&
                  !core.snapshot_installing && core.commit_index == maximum - 1 &&
                  core.last_stored == maximum - 1 && core.cache.last_index() == maximum - 1,
              "largest usable snapshot completes with representable replication state");
        return 1;
    }
}
int main() {
    try {
        auto cases = submission();
        cases += startup();
        cases += receiving_entries();
        cases += receiving_snapshot();
        std::cout << "core index limits: " << cases << " cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
