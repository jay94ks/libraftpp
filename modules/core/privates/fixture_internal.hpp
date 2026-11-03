#ifndef RAFTPP_FIXTURE_INTERNAL_HPP
#define RAFTPP_FIXTURE_INTERNAL_HPP
#include <raftpp/config.hpp>
#if RAFTPP_WITH_FIXTURE
#include <raftpp/node.hpp>
#include <raftpp/detail/core_state.hpp>
namespace raftpp::detail {
    // Internal observations and controlled election timers; no second driver.
    struct fixture_access {
        static core_state& core(node& value);
    };
}
#endif
#endif
