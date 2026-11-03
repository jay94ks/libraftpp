// C++ port of cowsql/raft src/timeout.c; see LICENSE.md.
#include "core_internal.hpp"

namespace raftpp::detail {

    error_code timeout(core_state& core) noexcept {
        if (std::holds_alternative<follower_state>(core.state)) {
            const auto self = core.configuration.get(core.id);
            if (self == nullptr) {
                core.tracing.log(trace_level::info,
                                 "  server not in current configuration -> stay follower");
                election_reset_timer(core);
                return {};
            }
            if (core.now < election_timer_expiration(core))
                return {};
            if (self->role != role::voter || core.snapshot_installing ||
                core.last_stored < core.trail.last_index()) {
                core.tracing.log(trace_level::info, self->role != role::voter
                                                        ? "  non-voter -> stay follower"
                                                    : core.snapshot_installing
                                                        ? "  installing snapshot -> stay follower"
                                                        : "  persisting entries -> stay follower");
                election_reset_timer(core);
                return {};
            }
            return convert_to_candidate(core, false);
        }
        if (std::holds_alternative<candidate_state>(core.state)) {
            if (core.now >= election_timer_expiration(core))
                return election_start(core);
            return {};
        }
        auto& leader = std::get<leader_state>(core.state);
        size_t contacts = 0;
        for (size_t i = 0; i < core.configuration.size(); ++i) {
            const auto& server = core.configuration.servers()[i];
            auto& progress = leader.progress[i];
            const bool recent = progress.contacted_recently(core.election_timer_start);
            if ((server.role == role::voter && recent) || server.id == core.id)
                ++contacts;
            if (!recent) {
                if (progress.mode == replication_mode::pipeline) {
                    logf(core.tracing, trace_level::info, source_location::current(),
                         "  server %llu is unreachable -> abort pipeline",
                         static_cast<unsigned long long>(server.id));
                    progress.to_probe();
                } else if (progress.mode == replication_mode::snapshot) {
                    logf(core.tracing, trace_level::info, source_location::current(),
                         "  server %llu is unreachable -> abort snapshot",
                         static_cast<unsigned long long>(server.id));
                    progress.abort_snapshot();
                }
            }
        }
        if (contacts > core.configuration.voter_count() / 2) {
            core.election_timer_start = core.now;
            core.update.flags |= update_timeout;
        } else if (core.now - core.election_timer_start >= core.election_timeout) {
            core.tracing.log(trace_level::info,
                             "  unable to contact majority of cluster -> step down");
            convert_to_follower(core);
            return {};
        }
        replication_heartbeat(core);
        if (leader.promotee_id != 0) {
            const auto duration = core.now - leader.round_start;
            if ((leader.round_number == core.max_catch_up_rounds &&
                 duration > core.election_timeout) ||
                duration > core.max_catch_up_round_duration) {
                logf(core.tracing, trace_level::info, source_location::current(),
                     "  server %llu catch-up too slow -> abort catch-up",
                     static_cast<unsigned long long>(leader.promotee_id));
                const auto i = core.configuration.index_of(leader.promotee_id);
                leader.promotee_id = 0;
                leader.round_index = 0;
                leader.round_number = 0;
                leader.round_start = 0;
                leader.progress[i].catch_up_abort();
            }
        }
        if (leader.transferee != 0 && core.now - leader.transfer_start >= core.election_timeout) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "  server %llu not replicating fast enough -> abort transfer",
                 static_cast<unsigned long long>(leader.transferee));
            leader.transferee = 0;
            leader.transferring = false;
        }
        return {};
    }

}
