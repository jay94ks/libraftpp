#include <raftpp/core.hpp>

#include <iostream>
#include <source_location>

namespace r = raftpp;
namespace {
    constexpr auto maximum_term = r::numeric_limits<r::term>::max();
    constexpr auto maximum_time = r::numeric_limits<r::uint64_t>::max();

    void check(bool value, r::string_view message,
               r::source_location where = r::source_location::current()) {
        if (!value)
            throw r::system_error(r::make_error_code(r::errc::invalid),
                                  r::string{where.file_name()} + ":" +
                                      std::to_string(where.line()) + ": " + r::string{message});
    }

    r::start_event starting(r::term term, unsigned voters = 3) {
        r::configuration members;
        for (unsigned id = 1; id <= voters; ++id)
            check(!members.add(id, "node-" + std::to_string(id), r::role::voter),
                  "configure numeric core");
        r::start_event value;
        value.term = term;
        value.metadata = r::snapshot_metadata{1, 1, std::move(members), 1};
        value.start_index = 2;
        return value;
    }

    r::error_code event(r::core_state& core, r::event_payload value) {
        return r::step(core, {core.now + 1, 8192, std::move(value)});
    }

    void initialize(r::core_state& core, r::term term, bool pre_vote = false, unsigned voters = 3,
                    r::uint64_t time = 1) {
        core.id = 1;
        core.address = "node-1";
        core.random = 1729;
        core.pre_vote = pre_vote;
        check(!r::step(core, {time, 8192, starting(term, voters)}), "start numeric core");
    }

    r::error_code expire(r::core_state& core) {
        return r::step(core, {r::next_timeout(core), 8192, r::timeout_event{}});
    }

    void vote(r::core_state& core, bool pre_vote) {
        r::request_vote_result result;
        result.term = maximum_term;
        result.vote_granted = true;
        result.pre_vote = pre_vote;
        check(!event(core, r::receive_event{{2, "node-2", result}}), "receive maximum-term vote");
    }

    void requests(const r::core_state& core, bool pre_vote) {
        check(core.update.messages.size() == 2, "send one election request to each other voter");
        for (const auto& message : core.update.messages) {
            const auto& request = std::get<r::request_vote>(message.payload);
            check(request.term == maximum_term && request.pre_vote == pre_vote &&
                      request.last_log_index == 1 && request.last_log_term == 1,
                  "largest election term advertises the durable prefix without wrapping");
        }
    }

    unsigned term_limits() {
        for (bool pre_vote : {false, true}) {
            r::core_state core;
            initialize(core, maximum_term, pre_vote);
            const auto random = core.random;
            const auto timer = core.election_timer_start;
            const auto deadline = r::next_timeout(core);
            const auto status = expire(core);
            if (status != r::errc::too_big)
                std::cerr << "exhausted election status=" << status.value()
                          << " term=" << core.current_term << '\n';
            check(status == r::errc::too_big && r::state(core) == r::node_state::follower &&
                      core.current_term == maximum_term && core.voted_for == 0 &&
                      core.random == random && core.election_timer_start == timer &&
                      r::next_timeout(core) == deadline && core.update.messages.empty() &&
                      !(core.update.flags &
                        (r::update_current_term | r::update_voted_for | r::update_state)),
                  "exhausted term rejects election before changing role, vote, timer, RNG or RPCs");
        }
        {
            r::core_state core;
            initialize(core, maximum_term - 1);
            check(!expire(core) && core.current_term == maximum_term && core.voted_for == 1,
                  "last representable actual election remains valid");
            requests(core, false);
            const auto random = core.random;
            const auto timer = core.election_timer_start;
            const auto deadline = r::next_timeout(core);
            check(expire(core) == r::errc::too_big && core.current_term == maximum_term &&
                      core.voted_for == 1 && r::state(core) == r::node_state::candidate &&
                      core.random == random && core.election_timer_start == timer &&
                      r::next_timeout(core) == deadline &&
                      std::get<r::candidate_state>(core.state).votes[0].grant &&
                      !std::get<r::candidate_state>(core.state).votes[1].grant &&
                      core.update.messages.empty(),
                  "candidate retry cannot wrap term or clear the last round");
            vote(core, false);
            check(r::state(core) == r::node_state::leader && core.current_term == maximum_term,
                  "last round can still win after rejected retry");
        }
        {
            r::core_state core;
            initialize(core, maximum_term - 1, true);
            check(!expire(core) && core.current_term == maximum_term - 1,
                  "pre-vote keeps current term");
            requests(core, true);
            check(!expire(core) && core.current_term == maximum_term - 1,
                  "last future-term pre-vote can retry");
            requests(core, true);
            vote(core, true);
            check(core.current_term == maximum_term &&
                      !std::get<r::candidate_state>(core.state).in_pre_vote,
                  "pre-vote quorum starts the last actual election");
            requests(core, false);
            vote(core, false);
            check(r::state(core) == r::node_state::leader && core.current_term == maximum_term,
                  "pre-vote can elect the maximum-term leader");
        }
        {
            r::core_state core;
            initialize(core, maximum_term, true);
            r::append_entries heartbeat;
            heartbeat.term = maximum_term;
            heartbeat.prev_log_index = 1;
            heartbeat.prev_log_term = 1;
            heartbeat.leader_commit = 1;
            check(!event(core, r::receive_event{{2, "node-2", heartbeat}}),
                  "establish maximum-term leader");
            const auto random = core.random;
            const auto deadline = r::next_timeout(core);
            r::timeout_now hint;
            hint.term = maximum_term;
            hint.last_log_index = 1;
            hint.last_log_term = 1;
            check(event(core, r::receive_event{{2, "node-2", hint}}) == r::errc::too_big &&
                      r::state(core) == r::node_state::follower &&
                      core.current_term == maximum_term &&
                      std::get<r::follower_state>(core.state).leader_id == 2 &&
                      core.random == random && r::next_timeout(core) == deadline &&
                      core.update.messages.empty(),
                  "disruptive leadership transfer cannot start an unrepresentable election");
        }
        for (bool pre_vote : {false, true}) {
            r::core_state core;
            initialize(core, maximum_term, pre_vote, 1);
            check(r::state(core) == r::node_state::leader && core.current_term == maximum_term &&
                      core.voted_for == 0,
                  "single-voter fast path still self-elects without increasing term");
            check(!event(core, r::submit_event{{{maximum_term, r::entry_type::command, {}}}}) &&
                      !event(core, r::persisted_entries_event{2}) && core.commit_index == 2 &&
                      core.trail.term_of(2) == maximum_term,
                  "maximum-term single voter commits commands");
        }
        {
            r::core_state core;
            initialize(core, maximum_term - 1);
            r::request_vote request;
            request.term = maximum_term;
            request.candidate_id = 2;
            request.last_log_index = 1;
            request.last_log_term = 1;
            check(!event(core, r::receive_event{{2, "node-2", request}}) &&
                      core.current_term == maximum_term && core.voted_for == 2 &&
                      (core.update.flags & r::update_current_term) &&
                      (core.update.flags & r::update_voted_for) &&
                      std::get<r::request_vote_result>(core.update.messages.front().payload)
                          .vote_granted,
                  "receiving a valid maximum term remains permitted");
        }
        return 8;
    }

    unsigned clock_limits() {
        {
            r::core_state core;
            core.id = 1;
            core.random = 1729;
            const auto status = r::step(core, {maximum_time - 1, 8192, starting(1)});
            if (status != r::errc::too_big)
                std::cerr << "terminal clock status=" << status.value() << " now=" << core.now
                          << " next=" << r::next_timeout(core) << '\n';
            check(status == r::errc::too_big && !core.started && core.now == 0 &&
                      core.current_term == 0 && core.random == 1729 && core.configuration.empty() &&
                      core.update.flags == 0 && !core.stepping,
                  "unsupported clock is rejected before startup or update mutation");
        }
        {
            r::core_state core;
            initialize(core, 1);
            const auto random = core.random;
            const auto flags = core.update.flags;
            const auto deadline = r::next_timeout(core);
            check(r::step(core, {maximum_time - 1, 0, r::timeout_event{}}) == r::errc::too_big &&
                      core.now == 1 && core.capacity == 8192 && core.random == random &&
                      core.update.flags == flags && r::next_timeout(core) == deadline &&
                      !core.stepping,
                  "rejected clock preserves prior update, capacity, state and deadline");
            check(!expire(core) && r::state(core) == r::node_state::candidate,
                  "normal clock can continue after a rejected event");
        }
        {
            r::core_state core;
            initialize(core, 1, false, 3, maximum_time - 2000);
            const auto random = core.random;
            const auto deadline = r::next_timeout(core);
            check(deadline > core.now && deadline <= maximum_time,
                  "largest admitted clock has an unwrapped deadline");
            check(r::set_election_timeout(core, 2000) == r::errc::too_big &&
                      core.election_timeout == 1000 && core.random == random &&
                      r::next_timeout(core) == deadline,
                  "timeout growth cannot overflow or consume random state");
            check(!r::set_election_timeout(core, 500) && r::next_timeout(core) > core.now,
                  "shorter timeout remains valid near the clock limit");
            check(!r::step(core, {maximum_time - 1000, 8192, r::timeout_event{}}) &&
                      r::state(core) == r::node_state::candidate &&
                      r::next_timeout(core) > core.now,
                  "shorter-timeout horizon permits a final valid election");
        }
        {
            r::core_state core;
            initialize(core, 1);
            check(!expire(core), "elect clock-boundary candidate");
            r::request_vote_result result;
            result.term = core.current_term;
            result.vote_granted = true;
            check(!event(core, r::receive_event{{2, "node-2", result}}) &&
                      r::state(core) == r::node_state::leader,
                  "elect clock-boundary leader");
            r::append_entries_result ack;
            ack.term = core.current_term;
            ack.last_log_index = 1;
            const auto time = maximum_time - 2000;
            check(!r::step(core, {time, 8192, r::receive_event{{2, "node-2", ack}}}) &&
                      !r::step(core, {time, 8192, r::timeout_event{}}) &&
                      r::state(core) == r::node_state::leader &&
                      core.election_timer_start == time && r::next_timeout(core) > time,
                  "leader quorum and heartbeat additions do not wrap at admitted clock edge");
            const auto deadline = r::next_timeout(core);
            const auto flags = core.update.flags;
            const auto count = core.update.messages.size();
            check(r::step(core, {time + 1, 8192, r::timeout_event{}}) == r::errc::too_big &&
                      core.now == time && core.update.flags == flags &&
                      core.update.messages.size() == count && r::next_timeout(core) == deadline,
                  "one tick beyond horizon preserves a leader's pending output");
        }
        return 4;
    }
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected a numeric test suite");
        const r::string_view suite{argv[1]};
        if (suite == "term")
            std::cout << "core term limits: " << term_limits() << " cases\n";
        else if (suite == "clock")
            std::cout << "core clock limits: " << clock_limits() << " cases\n";
        else
            check(false, "unknown numeric test suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
