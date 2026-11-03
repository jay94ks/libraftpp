#include <raftpp/detail/core.hpp>

#include <iostream>
#include <stdexcept>

namespace r = raftpp;
namespace d = raftpp::detail;

namespace allocation_failure {
    void after(std::ptrdiff_t count) noexcept;
}

namespace {

    void check(bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    r::configuration members() {
        r::configuration configuration;
        for (unsigned i = 1; i <= 3; ++i)
            check(!configuration.add(i, "node-" + std::to_string(i), r::role::voter), "members");
        return configuration;
    }

    r::entry configuration_entry(const r::configuration& configuration) {
        r::buffer buffer;
        check(!configuration.encode(buffer), "encode configuration");
        return {2, r::entry_type::change, r::shared_buffer{std::move(buffer)}};
    }

    void start(d::core_state& core, bool leader) {
        core.id = 1;
        auto first = configuration_entry(members());
        first.term = 1;
        d::core_event event{0, 8192, d::start_event{1, 0, {}, 1, {first}}};
        check(!d::step(core, event), "start core");
        if (!leader)
            return;
        event.time = d::next_timeout(core);
        event.payload = d::timeout_event{};
        check(!d::step(core, event), "start election");
        d::request_vote_result vote;
        vote.term = core.current_term;
        vote.vote_granted = true;
        event.payload = d::receive_event{{2, "node-2", vote}};
        check(!d::step(core, event), "elect leader");
        check(std::holds_alternative<d::leader_state>(core.state), "leader elected");
    }

    void test_failures() {
        for (unsigned operation = 0; operation < 4; ++operation) {
            unsigned failures = 0, successes = 0;
            for (std::ptrdiff_t limit = 0; limit < 80; ++limit) {
                d::core_state core;
                start(core, operation < 2);
                auto next = members();
                check(!next.add(4, "new-spare", r::role::spare), "add spare");
                d::core_event event;
                event.time = core.now + 1;
                event.capacity = 8192;
                if (operation == 0) {
                    event.payload = d::submit_event{
                        {{2, r::entry_type::command, r::shared_buffer{r::buffer{16}}},
                         {2, r::entry_type::command, r::shared_buffer{r::buffer{32}}}}};
                } else if (operation == 1) {
                    event.payload = d::submit_event{{configuration_entry(next)}};
                } else if (operation == 2) {
                    d::append_entries append;
                    append.term = 2;
                    append.prev_log_index = 1;
                    append.prev_log_term = 1;
                    append.leader_commit = 2;
                    append.entries.push_back(
                        std::make_shared<const r::entry>(configuration_entry(next)));
                    event.payload = d::receive_event{{2, "node-2", std::move(append)}};
                } else {
                    d::install_snapshot snapshot;
                    snapshot.term = 2;
                    snapshot.last_index = 20;
                    snapshot.last_term = 2;
                    snapshot.configuration = next;
                    snapshot.configuration_index = 10;
                    snapshot.data = r::shared_buffer{r::buffer{32}};
                    event.payload = d::receive_event{{2, "node-2", std::move(snapshot)}};
                }
                allocation_failure::after(limit);
                const auto status = d::step(core, event);
                allocation_failure::after(-1);
                if (status) {
                    ++failures;
                    check(status == r::errc::out_of_memory,
                          "allocation failure becomes error_code");
                    check(core.trail.last_index() == 1 && core.cache.last_index() == 1 &&
                              core.commit_index == 1,
                          "failed append rolls back log and commit");
                    check(core.configuration.size() == 3 &&
                              core.configuration_uncommitted_index == 0,
                          "failed append rolls back membership");
                    check(!(core.update.flags &
                            (d::update_entries | d::update_commit_index | d::update_snapshot)),
                          "failed append publishes no persistence or commit");
                    check(!d::step(core, event), "retry succeeds after allocation failure");
                } else {
                    ++successes;
                    // A failed per-peer send is deliberately nonfatal. The
                    // persistence request remains valid and a heartbeat retries.
                    check(core.update.flags &
                              (operation == 3 ? d::update_snapshot : d::update_entries),
                          "successful append requests persistence");
                }
                if (operation == 3) {
                    auto pending = std::move(core.update);
                    d::core_event completion{
                        event.time + 1, 8192,
                        d::persisted_snapshot_event{pending.snapshot.metadata, 0, true}};
                    allocation_failure::after(limit);
                    const auto completed = d::step(core, completion);
                    allocation_failure::after(-1);
                    check(
                        !completed && core.commit_index == 20 && core.last_stored == 20 &&
                            core.configuration.size() == 4 && !core.snapshot_installing,
                        "snapshot completion installs prepared values even if its reply allocation fails");
                    continue;
                }
                const auto last = operation == 0 ? 3U : 2U;
                check(core.trail.last_index() == last && core.cache.last_index() == last,
                      "retry caches agree");
                if (operation != 0)
                    check(core.configuration.size() == 4 &&
                              core.configuration_uncommitted_index == 2,
                          "successful retry installs pending membership");
                check(!core.stepping, "noexcept event boundary releases reentrancy guard");
            }
            check(failures >= 3 && successes > 0,
                  "failure sweep covered failing and successful allocations");
        }
    }

}

int main() {
    try {
        test_failures();
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
