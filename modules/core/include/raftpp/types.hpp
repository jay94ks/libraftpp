#ifndef RAFTPP_TYPES_HPP
#define RAFTPP_TYPES_HPP

#include <raftpp/common.hpp>

namespace raftpp {

    using server_id = uint64_t;
    using term = uint64_t;
    using log_index = uint64_t;

    struct leader_info {
        server_id id;
        string address;
    };

    enum class node_state : uint8_t { unavailable, follower, candidate, leader };
    enum class catch_up_status : uint16_t { none, running, aborted, finished };

    enum class role : uint8_t { standby, voter, spare };

    enum class entry_type : uint8_t { command = 1, barrier, change };

    [[nodiscard]] constexpr string_view state_name(node_state value) noexcept {
        switch (value) {
        case node_state::follower:
            return "follower";
        case node_state::candidate:
            return "candidate";
        case node_state::leader:
            return "leader";
        default:
            return {};
        }
    }
    [[nodiscard]] constexpr string_view role_name(role value) noexcept {
        switch (value) {
        case role::standby:
            return "stand-by";
        case role::voter:
            return "voter";
        case role::spare:
            return "spare";
        default:
            return {};
        }
    }

}

#endif // RAFTPP_TYPES_HPP
