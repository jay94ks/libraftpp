// Callback compatibility against cowsql/raft; see LICENSE.md.
#include <raftpp.hpp>
#include <cstring>
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
    struct machine final : r::state_machine {
        unsigned applied = 0;
        r::error_code apply(r::view<const r::byte>, r::shared<void>& result) override {
            ++applied;
            result.reset();
            return {};
        }
        r::error_code restore(r::shared_buffer) override {
            return r::errc::io_error;
        }
        r::error_code snapshot(r::vector<r::shared_buffer>&) override {
            return r::errc::io_error;
        }
    };
    struct outcome {
        int status = -1000;
        unsigned calls = 0;
    };
    struct driver {
        r::array<outcome, 64> outcomes;
        r::fixture actual;
        r::vector<r::shared<machine>> machines;
        unsigned events = 0;
        bool observing = false;
        r::vector<r::array<r::uint64_t, 7>> steps;
        r::size_t compared_steps = 0;
        driver(bool forced, unsigned voters = 3, bool observe = false) {
            for (unsigned i = 0; i < 3; ++i) {
                auto fsm = std::make_shared<machine>();
                r::node_options options;
                // Test the explicit barrier policy independently of executable names.
                options.force_barrier = forced;
                check(!actual.grow(fsm, options), "grow compatibility node");
                machines.push_back(fsm);
            }
            r::configuration members;
            check(!actual.configuration(voters, members) && !actual.bootstrap(members),
                  "bootstrap compatibility fixture");
            if (observe) {
                observing = true;
                for (unsigned i = 0; i < 3; ++i)
                    check(!actual.get(i).set_step_callback([this, i] {
                        const auto& node = actual.get(i);
                        steps.push_back({i + 1, static_cast<unsigned>(node.state()),
                                         node.current_term(), node.voted_for(), node.last_index(),
                                         node.commit_index(), node.last_applied()});
                    }),
                          "register step observer");
            }
            check(!actual.start(), "start compatibility fixture");
            compare();
        }
        ~driver() {
            if (observing) {
                for (unsigned i = 0; i < actual.size(); ++i)
                    (void)actual.get(i).set_step_callback();
            }
            actual.close();
        }
        void compare() {
            check(!actual.error(), "fixture retains consensus safety");
            for (unsigned i = 0; i < actual.size(); ++i) {
                const auto& node = actual.get(i);
                check(node.last_applied() <= node.commit_index() &&
                          node.commit_index() <= node.last_index(),
                      "applied/committed/log order");
                const auto members = node.configuration();
                for (r::size_t j = 0; j < members.size(); ++j) {
                    const auto& member = members.servers()[j];
                    check(member.id && !member.address.empty() && members.index_of(member.id) == j,
                          "owning unique membership values");
                }
            }
            for (const auto& outcome : outcomes)
                check(outcome.calls <= 1, "accepted request completes at most once");
            if (observing) {
                while (compared_steps < steps.size()) {
                    const auto& row = steps[compared_steps++];
                    check(row[0] >= 1 && row[0] <= actual.size() && row[1] <= 3 &&
                              row[6] <= row[5] && row[5] <= row[4],
                          "step notification carries valid state before application");
                }
            }
        }
        void step() {
            const auto before = actual.time();
            const auto event = actual.step();
            ++events;
            check(event && event->time >= before && event->server_index < actual.size(),
                  "fixture event has valid identity and monotonic time");
            compare();
        }
        template <typename F> void until(F stop) {
            unsigned remaining = 4000;
            while (!stop() && remaining--)
                step();
            check(stop(), "compatibility operation completed");
        }
        void elected(bool forced) {
            until([&] { return actual.leader_index() == 0; });
            const auto index = forced ? 2U : 1U;
            until([&] { return actual.get(0).last_applied() == index; });
            check(actual.get(0).last_index() == index,
                  "driver barrier policy matches executable mode");
            until([&] {
                return actual.get(1).last_applied() == index &&
                       actual.get(2).last_applied() == index;
            });
        }
        int submit(unsigned slot, unsigned operation, r::server_id id = 0,
                   r::role role = r::role::spare, r::string address = "", unsigned index = 0) {

            auto callback = [&, slot](r::error_code status) {
                outcomes[slot].status = status.value();
                ++outcomes[slot].calls;
            };
            r::error_code status;
            auto& node = actual.get(index);
            switch (operation) {
            case 0:
                status =
                    node.apply(r::buffer{8}, [callback](auto status, auto) { callback(status); });
                break;
            case 1:
                status = node.barrier(callback);
                break;
            case 2:
                status = node.add(id, address, callback);
                break;
            case 3:
                status = node.assign(id, role, callback);
                break;
            case 4:
                status = node.remove(id, callback);
                break;
            case 5:
                status = node.transfer(id, [callback] { callback({}); });
                break;
            default:
                throw std::runtime_error("unknown compatibility operation");
            }
            compare();
            return status.value();
        }
        void completed(unsigned slot) {
            until([&] { return outcomes[slot].calls != 0; });
            check(outcomes[slot].status == 0 && outcomes[slot].calls == 1,
                  "request completed once successfully");
        }
    };
    void barrier(bool forced) {
        driver value{forced};
        value.elected(forced);
        check(!value.submit(0, 0) && !value.submit(1, 1), "command and barrier accepted");
        value.completed(0);
        value.completed(1);
        const auto last = forced ? 4U : 3U;
        value.until([&] {
            return value.actual.get(0).last_applied() == last &&
                   value.actual.get(1).last_applied() == last &&
                   value.actual.get(2).last_applied() == last;
        });
        for (const auto& machine : value.machines)
            check(machine->applied == 1, "barriers do not reach the FSM");
    }
    void membership(bool forced) {
        driver value{forced, 2};
        value.elected(forced);
        check(value.submit(0, 3, 99, static_cast<r::role>(99)) ==
                  static_cast<int>(r::errc::bad_role),
              "bad role before unknown ID");
        check(value.submit(1, 3, 99, r::role::voter) == static_cast<int>(r::errc::not_found),
              "unknown assignment target");
        check(value.submit(2, 3, 1, r::role::voter) == static_cast<int>(r::errc::bad_role),
              "same role rejected");
        check(!value.submit(3, 3, 3, r::role::voter), "promote standby");
        check(value.submit(4, 3, 2, static_cast<r::role>(99)) ==
                  static_cast<int>(r::errc::bad_role),
              "bad role precedes pending change");
        check(value.submit(5, 2, 1, r::role::spare, "duplicate") ==
                  static_cast<int>(r::errc::duplicate_id),
              "duplicate ID precedes pending change");
        check(value.submit(6, 2, 4, r::role::spare, "1") ==
                  static_cast<int>(r::errc::duplicate_address),
              "duplicate address precedes pending change");
        check(value.submit(7, 4, 99) == static_cast<int>(r::errc::bad_id),
              "missing removal target precedes pending change");
        check(value.submit(8, 3, 2, r::role::standby) == static_cast<int>(r::errc::cannot_change),
              "otherwise valid assignment waits for change");
        value.completed(3);
        check(!value.submit(9, 2, 4, r::role::spare, "4"), "add spare");
        value.completed(9);
        check(!value.submit(10, 4, 4), "remove spare");
        value.completed(10);
        check(!value.submit(11, 3, 3, r::role::standby), "demote voter");
        value.completed(11);
    }
    void step_notifications(bool forced) {
        driver value{forced, 2, true};
        value.elected(forced);
        const auto count = value.steps.size();
        check(value.submit(0, 0, 0, r::role::spare, "", 1) == static_cast<int>(r::errc::not_leader),
              "rejected submission emits no successful-step notification");
        check(value.steps.size() == count, "no observer on rejected step");
        check(!value.submit(1, 0), "observed command");
        value.completed(1);
        check(!value.submit(2, 1), "observed barrier");
        value.completed(2);
        check(!value.submit(3, 3, 3, r::role::voter), "observed promotion");
        value.completed(3);
        check(!value.submit(4, 2, 4, r::role::spare, "4"), "observed addition");
        value.completed(4);
        check(!value.submit(5, 4, 4), "observed removal");
        value.completed(5);
        value.compare();
        check(value.steps.size() > 30 && value.compared_steps == value.steps.size(),
              "validated complete observer stream");
        std::cout << "step notifications: " << value.compared_steps << " checked rows, "
                  << value.events << " events\n";
    }
    void transfer(bool forced) {
        driver value{forced, 3};
        value.elected(forced);
        check(!value.submit(0, 0), "command before transfer");
        value.completed(0);
        value.until([&] {
            return value.actual.get(1).last_applied() == value.actual.get(0).last_applied();
        });
        check(!value.submit(1, 5, 2), "transfer accepted");
        value.completed(1);
        value.until([&] { return value.actual.leader_index() == 1; });
        check(!value.submit(2, 0, 0, r::role::spare, "", 1), "new leader accepts command");
        value.completed(2);
        const auto last = forced ? 5U : 3U;
        value.until([&] {
            return value.actual.get(0).last_applied() == last &&
                   value.actual.get(1).last_applied() == last &&
                   value.actual.get(2).last_applied() == last;
        });
        for (const auto& machine : value.machines)
            check(machine->applied == 2, "every FSM applies both commands across transfer");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite argument");
        const bool forced = RAFTPP_TEST_FORCE_BARRIER;
        const r::string_view suite{argv[1]};
        if (suite == "barrier")
            barrier(forced);
        else if (suite == "membership")
            membership(forced);
        else if (suite == "transfer")
            transfer(forced);
        else if (suite == "step_notifications")
            step_notifications(forced);
        else
            check(false, "unknown suite");
        std::cout << "legacy " << suite << " mode=" << (forced ? "forced" : "normal")
                  << " passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
