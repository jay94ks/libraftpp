// C++ port of cowsql/raft src/client.c; see LICENSE.md.
#include "core_internal.hpp"

namespace raftpp::detail {

    error_code submit_entries(core_state& core, view<const entry> entries) noexcept {
        auto leader = std::get_if<leader_state>(&core.state);
        if (leader == nullptr || leader->transferee != 0)
            return errc::not_leader;
        if (entries.empty())
            return errc::invalid;
        if (core.capacity_threshold != 0) {
            size_t reporting = 0, healthy = 0;
            for (size_t i = 0; i < core.configuration.size(); ++i) {
                const auto& server = core.configuration.servers()[i];
                const auto& progress = leader->progress[i];
                if (server.role != role::voter || !(progress.features & feature_capacity))
                    continue;
                ++reporting;
                if (progress.capacity >= core.capacity_threshold)
                    ++healthy;
            }
            const auto half = core.configuration.voter_count() / 2;
            if (reporting > half && healthy <= half)
                return errc::no_space;
        }
        const auto previous = core.trail.last_index();
        // Replication keeps last_index + 1 in the same type. The terminal
        // value may be the next index, but must never become a log index.
        if (entries.size() >= numeric_limits<log_index>::max() - previous)
            return errc::too_big;
        const auto index = previous + 1;
        size_t changes = 0;
        for (const auto& entry : entries)
            if (entry.type == entry_type::change)
                ++changes;
        if (changes > 1 || (changes != 0 && (core.configuration_uncommitted_index != 0 ||
                                             leader->promotee_id != 0))) {
            return errc::cannot_change;
        }
        try {
            append_transaction transaction{core, index, changes != 0};
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                if (entry.type == entry_type::change &&
                    (core.configuration_uncommitted_index != 0 || leader->promotee_id != 0)) {
                    return errc::cannot_change;
                }
                if (auto error = append_cached(core, entry)) {
                    if (i != 0)
                        truncate_cached(core, index);
                    return error;
                }
                if (entry.type == entry_type::change) {
                    if (auto error = uncommitted_change(core, index + i, entry)) {
                        truncate_cached(core, index);
                        return error;
                    }
                }
            }
            if (auto error = persist_entries(core, index, entries.size())) {
                truncate_cached(core, index);
                return error;
            }
            transaction.commit();
            replication_heartbeat(core);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code catch_up(core_state& core, server_id id) noexcept {
        auto leader = std::get_if<leader_state>(&core.state);
        if (leader == nullptr)
            return errc::not_leader;
        const auto i = core.configuration.index_of(id);
        if (i == core.configuration.size() || id == core.id)
            return errc::bad_id;
        if (leader->progress[i].catch_up == catch_up_status::running)
            return errc::busy;
        leader->promotee_id = id;
        leader->round_number = 1;
        leader->round_index = core.trail.last_index();
        leader->round_start = core.now;
        leader->progress[i].catch_up_start();
        (void)replication_progress(core, i);
        return {};
    }

    error_code transfer_leadership(core_state& core, server_id id) noexcept {
        auto leader = std::get_if<leader_state>(&core.state);
        if (leader == nullptr || leader->transferee != 0)
            return errc::not_leader;
        if (id == 0) {
            for (size_t i = 0; i < core.configuration.size(); ++i) {
                const auto& server = core.configuration.servers()[i];
                if (server.id == core.id || server.role != role::voter)
                    continue;
                id = server.id;
                if (leader->progress[i].match_index == core.trail.last_index())
                    break;
            }
            if (id == 0)
                return errc::not_found;
        }
        const auto self = core.configuration.get(id);
        if (self == nullptr || id == core.id || self->role != role::voter)
            return errc::bad_id;
        const auto i = core.configuration.index_of(id);
        leader->transferee = id;
        leader->transfer_start = core.now;
        if (leader->progress[i].match_index == core.trail.last_index()) {
            if (auto error = transfer_start(core)) {
                leader->transferee = 0;
                return error;
            }
        }
        return {};
    }

}
