#include <raftpp/core.hpp>
#include <iostream>

namespace r = raftpp;
namespace {
    void check(bool value, r::string_view message) {
        if (!value)
            throw std::runtime_error(r::string{message});
    }
    void comparisons() {
        unsigned count = 0;
        for (unsigned state = 1; state <= 3; ++state)
            for (unsigned role = 0; role <= 2; ++role)
                for (r::server_id self : {1U, 99U})
                    for (r::server_id known : {0U, 2U})
                        for (r::server_id transferee : {0U, 2U})
                            for (r::server_id target : {1U, 2U, 3U, 99U})
                                for (unsigned catching = 0; catching < 4; ++catching) {
                                    r::core_state core;
                                    core.id = self;
                                    core.address = "node-1";
                                    check(
                                        !core.configuration.add(1, "node-1",
                                                                static_cast<r::role>(role)) &&
                                            !core.configuration.add(2, "node-2", r::role::voter) &&
                                            !core.configuration.add(3, "node-3", r::role::standby),
                                        "configure query state");
                                    if (state == 1) {
                                        auto& value = std::get<r::follower_state>(core.state);
                                        value.leader_id = known;
                                        if (known)
                                            value.leader_address = "learned-leader";
                                    } else if (state == 2)
                                        core.state.emplace<r::candidate_state>();
                                    else {
                                        auto& value = core.state.emplace<r::leader_state>();
                                        value.transferee = transferee;
                                        value.progress.resize(3);
                                        for (unsigned i = 0; i < 3; ++i) {
                                            value.progress[i].match_index = (i + 1) * 11;
                                            value.progress[i].catch_up =
                                                static_cast<r::catch_up_status>(catching);
                                        }
                                    }
                                    core.update.flags = r::update_messages | r::update_entries;
                                    core.random = 7;
                                    core.now = 123;
                                    check(static_cast<unsigned>(r::state(core)) == state,
                                          "query returns assembled role state");
                                    const auto actual_role = r::local_role(core);
                                    check(self == 99 ? !actual_role
                                                     : actual_role && static_cast<unsigned>(
                                                                          *actual_role) == role,
                                          "local role follows membership");
                                    const auto leader = r::leader(core);
                                    const r::server_id leader_id = state == 1 ? known
                                                                   : state == 3 && !transferee
                                                                       ? self
                                                                       : 0;
                                    check((leader ? leader->id : 0) == leader_id &&
                                              (!leader ||
                                               leader->address ==
                                                   (state == 1 ? "learned-leader" : "node-1")),
                                          "leader visibility and owning address");
                                    check(r::transferee(core) == (state == 3 ? transferee : 0),
                                          "transferee is leader-only");
                                    const r::error_code wanted =
                                        state != 3     ? r::make_error_code(r::errc::not_leader)
                                        : target == 99 ? r::make_error_code(r::errc::bad_id)
                                                       : r::error_code{};
                                    r::log_index match = 999;
                                    check(
                                        r::match_index(core, target, match) == wanted &&
                                            match == (wanted ? 999 : target * 11),
                                        "match query validates leadership/id and preserves output on failure");
                                    auto catching_state = static_cast<r::catch_up_status>(99);
                                    check(
                                        r::catch_up(core, target, catching_state) == wanted &&
                                            static_cast<unsigned>(catching_state) ==
                                                (wanted ? 99 : catching),
                                        "catch-up query validates leadership/id and preserves output on failure");
                                    check(
                                        core.update.flags ==
                                                (r::update_messages | r::update_entries) &&
                                            core.random == 7 && core.now == 123,
                                        "read-only queries preserve update, clock and random state");
                                    ++count;
                                }
        check(count == 1152, "all 1152 query combinations checked");
    }
    void ownership() {
        r::optional<r::leader_info> result;
        {
            r::core_state core;
            core.id = 1;
            core.address = r::string(256, 's');
            auto& follower = std::get<r::follower_state>(core.state);
            follower.leader_id = 2;
            follower.leader_address = r::string(256, 'p');
            result = r::leader(core);
            follower.leader_address.clear();
            check(result && result->id == 2 && result->address == r::string(256, 'p'),
                  "leader result owns an independent address");
            core.state.emplace<r::leader_state>();
            result = r::leader(core);
            core.address.clear();
            check(result && result->id == 1 && result->address == r::string(256, 's'),
                  "self-leader result owns its address");
        }
        check(result && result->address == r::string(256, 's'), "leader result outlives its core");
        r::core_state malformed;
        malformed.id = 1;
        check(!malformed.configuration.add(1, "node-1", r::role::voter),
              "configure missing progress case");
        malformed.state.emplace<r::leader_state>();
        r::log_index match = 777;
        auto status = r::catch_up_status::aborted;
        check(
            r::match_index(malformed, 1, match) == r::errc::invalid && match == 777 &&
                r::catch_up(malformed, 1, status) == r::errc::invalid &&
                status == r::catch_up_status::aborted,
            "inconsistent manually assembled state rejects missing progress without out-of-bounds reads");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "query suite argument");
        const r::string_view suite{argv[1]};
        if (suite == "contracts")
            comparisons();
        else if (suite == "ownership")
            ownership();
        else
            check(false, "unknown query suite");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
