// C++ port of cowsql/raft src/raft.c and restore.c; see LICENSE.md.
#include "core_internal.hpp"

#include <algorithm>
#include "assert.hpp"

namespace raftpp::detail {

    error_code append_cached(core_state& core, const entry& entry) noexcept {
        const auto index = core.cache.last_index();
        if (auto error = core.cache.append(entry))
            return error;
        if (auto error = core.trail.append(entry.term)) {
            (void)core.cache.truncate(index + 1);
            return error;
        }
        return {};
    }

    void truncate_cached(core_state& core, log_index index) noexcept {
        if (core.trail.has_entry(index))
            (void)core.trail.truncate(index);
        (void)core.cache.truncate(index);
    }

    error_code persist_entries(core_state& core, log_index index, size_t count) noexcept {
        RAFTPP_ASSERT((core.update.flags & update_entries) == 0);
        if (auto error = core.cache.acquire(index, core.update.entries.batch, count))
            return error;
        RAFTPP_ASSERT(core.update.entries.batch.size() == count);
        core.update.entries.index = index;
        core.update.flags |= update_entries;
        return {};
    }

    void restore_snapshot_owned(core_state& core, const snapshot_metadata& metadata,
                                configuration current, configuration committed) noexcept {
        core.configuration = std::move(current);
        core.configuration_committed = std::move(committed);
        core.configuration_committed_index = metadata.configuration_index;
        core.configuration_uncommitted_index = 0;
        core.configuration_last_snapshot_index = metadata.configuration_index;
        core.commit_index = core.last_stored = metadata.index;
        core.update.flags |= update_commit_index;
    }

    error_code restore_snapshot(core_state& core, const snapshot_metadata& metadata) noexcept {
        try {
            restore_snapshot_owned(core, metadata, metadata.configuration, metadata.configuration);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    namespace {

        void trace_start(const core_state& core) noexcept {
            if (!core.tracing.enabled())
                return;
            array<char, 512> text{};
            size_t used = 0;
            auto append = [&](const char* format, auto... values) {
                const int count =
                    std::snprintf(text.data() + used, text.size() - used, format, values...);
                if (count > 0)
                    used += std::min(static_cast<size_t>(count), text.size() - used - 1);
            };
            const auto count = core.trail.size();
            const auto snapshot = core.trail.snapshot_index();
            if (core.current_term == 0)
                append("%s", "no state");
            else {
                append("term %llu", static_cast<unsigned long long>(core.current_term));
                if (snapshot || count)
                    append("%s", ", ");
                if (core.voted_for)
                    append("voted for %llu, ", static_cast<unsigned long long>(core.voted_for));
                if (snapshot) {
                    append("1 snapshot (%llu^%llu)", static_cast<unsigned long long>(snapshot),
                           static_cast<unsigned long long>(core.trail.snapshot_term()));
                    if (count)
                        append("%s", ", ");
                }
                if (count) {
                    const auto first = core.trail.last_index() - count + 1;
                    if (count == 1)
                        append("1 entry (%llu^%llu)", static_cast<unsigned long long>(first),
                               static_cast<unsigned long long>(core.trail.term_of(first)));
                    else
                        append("%llu entries (%llu^%llu..%llu^%llu)",
                               static_cast<unsigned long long>(count),
                               static_cast<unsigned long long>(first),
                               static_cast<unsigned long long>(core.trail.term_of(first)),
                               static_cast<unsigned long long>(core.trail.last_index()),
                               static_cast<unsigned long long>(core.trail.last_term()));
                }
            }
            logf(core.tracing, trace_level::info, source_location::current(), "> %s", text.data());
        }

        error_code handle(core_state& core, const start_event& event) {
            if (core.started || !std::holds_alternative<follower_state>(core.state) ||
                !core.cache.empty() || core.trail.size() != 0 || event.start_index == 0)
                return errc::invalid;
            if (event.term == 0 &&
                (event.voted_for != 0 || event.metadata || !event.entries.empty()))
                return errc::invalid;
            if ((event.metadata && event.metadata->index == numeric_limits<log_index>::max()) ||
                event.entries.size() > numeric_limits<log_index>::max() - event.start_index)
                return errc::too_big;
            core.current_term = event.term;
            core.voted_for = event.voted_for;
            log_index snapshot_index = 0;
            term snapshot_term = 0;
            if (event.metadata) {
                snapshot_index = event.metadata->index;
                snapshot_term = event.metadata->term;
                if (snapshot_index == 0 || snapshot_term == 0)
                    return errc::invalid;
                if (auto error = restore_snapshot(core, *event.metadata))
                    return error;
            } else if (!event.entries.empty()) {
                if (event.start_index != 1 || event.entries[0].type != entry_type::change)
                    return errc::invalid;
                core.commit_index = 1;
                core.update.flags |= update_commit_index;
            }
            if (auto error = core.trail.start(snapshot_index, snapshot_term, event.start_index))
                return error;
            if (auto error = core.cache.start(snapshot_index, snapshot_term, event.start_index))
                return error;
            core.last_stored = event.start_index - 1;
            borrowed<const entry> last_configuration = nullptr;
            log_index configuration_index = 0;
            for (const auto& entry : event.entries) {
                if (auto error = append_cached(core, entry))
                    return error;
                ++core.last_stored;
                if (entry.type == entry_type::change &&
                    core.last_stored > core.configuration_committed_index) {
                    if (last_configuration != nullptr) {
                        if (auto error = core.configuration_committed.decode(
                                last_configuration->payload.bytes()))
                            return error;
                        core.configuration_committed_index = configuration_index;
                        // Preserve the reference's choice not to advance commit_index here.
                    }
                    last_configuration = &entry;
                    configuration_index = core.last_stored;
                }
            }
            if (last_configuration != nullptr) {
                if (auto error = core.configuration.decode(last_configuration->payload.bytes()))
                    return error;
                if (configuration_index == 1) {
                    core.configuration_committed = core.configuration;
                    core.configuration_committed_index = 1;
                } else {
                    core.configuration_uncommitted_index = configuration_index;
                }
            }
            trace_start(core);
            election_reset_timer(core);
            auto& follower = std::get<follower_state>(core.state);
            follower.leader_id = 0;
            follower.leader_address.clear();
            const auto self = core.configuration.get(core.id);
            if (self != nullptr && self->role == role::voter &&
                core.configuration.voter_count() == 1) {
                if (auto error = convert_to_candidate(core, false))
                    return error;
                replication_heartbeat(core);
            }
            core.started = true;
            return {};
        }

        error_code handle(core_state& core, const receive_event& event) {
            static constexpr array<const char*, 6> names{
                "append entries",      "append entries result", "request vote",
                "request vote result", "install snapshot",      "timeout now"};
            const auto i = event.rpc.payload.index();
            logf(core.tracing, trace_level::info, source_location::current(),
                 "> recv %s from server %llu", i < names.size() ? names[i] : "unknown message",
                 static_cast<unsigned long long>(event.rpc.server_id));
            return receive_message(core, event.rpc);
        }
        error_code handle(core_state& core, const persisted_entries_event& event) {
            const auto count = event.index > core.last_stored ? event.index - core.last_stored : 0;
            const auto first = core.last_stored + 1;
            if (count == 1)
                logf(core.tracing, trace_level::info, source_location::current(),
                     "> persisted 1 entry (%llu^%llu)", static_cast<unsigned long long>(first),
                     static_cast<unsigned long long>(core.trail.term_of(first)));
            else
                logf(core.tracing, trace_level::info, source_location::current(),
                     "> persisted %llu entry (%llu^%llu..%llu^%llu)",
                     static_cast<unsigned long long>(count), static_cast<unsigned long long>(first),
                     static_cast<unsigned long long>(core.trail.term_of(first)),
                     static_cast<unsigned long long>(event.index),
                     static_cast<unsigned long long>(core.trail.term_of(event.index)));
            return persisted_entries(core, event.index);
        }
        error_code handle(core_state& core, const persisted_snapshot_event& event) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "> persisted snapshot (%llu^%llu)",
                 static_cast<unsigned long long>(event.metadata.index),
                 static_cast<unsigned long long>(event.metadata.term));
            return persisted_snapshot(core, event);
        }
        error_code handle(core_state& core, const configuration_event& event) {
            logf(core.tracing, trace_level::trace, source_location::current(),
                 "apply configuration at %llu (%zu servers)",
                 static_cast<unsigned long long>(event.index), event.configuration.size());
            return apply_configuration(core, event);
        }
        error_code handle(core_state& core, const snapshot_event& event) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "> new snapshot (%llu^%llu), %u trailing entr%s",
                 static_cast<unsigned long long>(event.metadata.index),
                 static_cast<unsigned long long>(event.metadata.term), event.trailing,
                 event.trailing == 1 ? "y" : "ies");
            return take_snapshot(core, event);
        }
        error_code handle(core_state& core, const timeout_event&) {
            logf(core.tracing, trace_level::info, source_location::current(), "> timeout as %s",
                 core.state.index() == 0   ? "follower"
                 : core.state.index() == 1 ? "candidate"
                                           : "leader");
            return timeout(core);
        }
        error_code handle(core_state& core, const submit_event& event) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "> submit %zu new client entr%s", event.entries.size(),
                 event.entries.size() == 1 ? "y" : "ies");
            return submit_entries(core, event.entries);
        }
        error_code handle(core_state& core, const catch_up_event& event) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "> catch-up server %llu", static_cast<unsigned long long>(event.id));
            return catch_up(core, event.id);
        }
        error_code handle(core_state& core, const transfer_event& event) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "> transfer leadership to %llu", static_cast<unsigned long long>(event.id));
            return transfer_leadership(core, event.id);
        }

    }

    error_code step(core_state& core, const core_event& event) noexcept {
        if (core.stepping || event.payload.valueless_by_exception() || event.time < core.now ||
            core.election_timeout == 0 ||
            core.election_timeout > numeric_limits<uint32_t>::max() / 2)
            return errc::invalid;
        if (!core.started && !std::holds_alternative<start_event>(event.payload))
            return errc::invalid;
        // Reserve the entire randomized [election, 2*election] timer range.
        // Reject before replacing the previous update or advancing the clock.
        if (event.time > numeric_limits<time>::max() - 2 * static_cast<time>(core.election_timeout))
            return errc::too_big;
        core.stepping = true;
        core.update = {};
        core.now = event.time;
        core.capacity = event.capacity;
        if (auto leader = std::get_if<leader_state>(&core.state)) {
            const auto i = core.configuration.index_of(core.id);
            if (i < leader->progress.size()) {
                leader->progress[i].features = feature_capacity;
                leader->progress[i].capacity = core.capacity;
            }
        }
        error_code result;
        try {
            result = std::visit([&](const auto& payload) { return handle(core, payload); },
                                event.payload);
        } catch (const bad_alloc&) {
            result = errc::out_of_memory;
        } catch (const length_error&) {
            result = errc::too_big;
        }
        logf(core.tracing, trace_level::debug, source_location::current(),
             "state=%zu term=%llu stored=%llu commit=%llu flags=%u messages=%zu",
             core.state.index(), static_cast<unsigned long long>(core.current_term),
             static_cast<unsigned long long>(core.last_stored),
             static_cast<unsigned long long>(core.commit_index), core.update.flags,
             core.update.messages.size());
        core.stepping = false;
        if (result)
            logf(core.tracing, trace_level::error, source_location::current(),
                 "raft event failed: %d", result.value());
        return result;
    }

    time next_timeout(const core_state& core) noexcept {
        if (auto leader = std::get_if<leader_state>(&core.state)) {
            time last_send = never;
            for (const auto& peer : leader->progress)
                last_send = std::min(last_send, peer.last_message_sent());
            const auto quorum_timeout = core.election_timer_start + core.election_timeout;
            return last_send == never
                       ? quorum_timeout
                       : std::min(last_send + core.heartbeat_timeout, quorum_timeout);
        }
        return election_timer_expiration(core);
    }

}
