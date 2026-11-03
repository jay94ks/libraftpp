#include <raftpp/core.hpp>

#include <iostream>
#include <source_location>

namespace r = raftpp;
namespace {
    void check(bool value, r::string_view message,
               r::source_location where = r::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    void event(r::core_state& core, r::event_payload value) {
        check(!r::step(core, {core.now + 1, 8192, std::move(value)}), "event accepted");
    }
    void initialize(r::core_state& core, unsigned kind) {
        core.id = 1;
        core.address = "node-1";
        core.pre_vote = true;
        r::configuration members;
        for (unsigned i = 1; i <= (kind == 2 ? 1U : 3U); ++i)
            check(!members.add(i, "node-" + std::to_string(i), r::role::voter), "member");
        r::start_event start;
        start.term = 2;
        start.metadata = r::snapshot_metadata{1, 1, std::move(members), 1};
        start.start_index = 2;
        event(core, std::move(start));
        if (kind == 1) {
            check(!r::step(core, {r::next_timeout(core), 8192, r::timeout_event{}}) &&
                      r::state(core) == r::node_state::candidate,
                  "enter pre-vote");
            r::append_entries request;
            request.term = 3;
            request.prev_log_index = 1;
            request.prev_log_term = 1;
            event(core, r::receive_event{{2, "node-2", std::move(request)}});
            check(r::state(core) == r::node_state::follower, "step down before delayed reply");
        }
    }
}
int main() {
    try {
        unsigned cases = 0;
        for (unsigned kind = 0; kind < 3; ++kind)
            for (unsigned version : {1U, 2U})
                for (bool pre_vote : {false, true})
                    for (bool higher : {false, true}) {
                        r::core_state core;
                        initialize(core, kind);
                        const auto term = core.current_term, vote = core.voted_for;
                        const auto state = r::state(core);
                        r::request_vote_result reply;
                        reply.version = version;
                        reply.term = higher ? term + 1 : term - 1;
                        reply.pre_vote = pre_vote;
                        reply.vote_granted = false;
                        event(core, r::receive_event{{2, "node-2", reply}});
                        const bool bump = higher && !(version > 1 && pre_vote);
                        if (core.current_term != term + (bump ? 1 : 0))
                            std::cerr << "kind=" << kind << " version=" << version
                                      << " pre_vote=" << pre_vote << " higher=" << higher
                                      << " before=" << term << " after=" << core.current_term
                                      << '\n';
                        check(core.current_term == term + (bump ? 1 : 0),
                              "only actual higher-term replies advance term");
                        check(r::state(core) == (bump ? r::node_state::follower : state) &&
                                  core.voted_for == (bump ? 0 : vote),
                              "state and vote preserved for delayed pre-vote");
                        check(bool(core.update.flags & r::update_current_term) == bump &&
                                  bool(core.update.flags & r::update_voted_for) == bump,
                              "virtual terms never request persistence");
                        check(core.trail.last_index() == 1 && core.commit_index == 1 &&
                                  core.update.messages.empty(),
                              "delayed replies do not change logs or send messages");
                        ++cases;
                    }
        std::cout << "stale vote replies: " << cases << " cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
