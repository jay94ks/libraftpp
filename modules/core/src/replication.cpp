// C++ port of cowsql/raft src/replication.c and recv_append_entries*.c;
// see LICENSE.md.
#include "core_internal.hpp"

#include <algorithm>
#include "assert.hpp"

namespace raftpp::detail {

    namespace {

        error_code send_append(core_state& core, size_t i, log_index previous, term previous_term,
                               bool heartbeat) noexcept {
            auto& progress = std::get<leader_state>(core.state).progress[i];
            const auto& server = core.configuration.servers()[i];
            append_entries request;
            request.term = core.current_term;
            request.prev_log_index = previous;
            request.prev_log_term = previous_term;
            request.leader_commit = core.commit_index;
            const auto next = previous + 1;
            if (!heartbeat && core.trail.has_entry(next)) {
                const auto inflight = next - progress.match_index - 1;
                if (inflight < core.max_inflight_entries) {
                    const auto count = std::min<log_index>(core.trail.last_index() - next + 1,
                                                           core.max_inflight_entries - inflight);
                    if (auto error =
                            core.cache.acquire(next, request.entries, static_cast<size_t>(count)))
                        return error;
                    RAFTPP_ASSERT(request.entries.size() == count);
                }
            }
            const auto count = request.entries.size();
            if (auto error =
                    enqueue_message(core.update, server.id, server.address, std::move(request)))
                return error;
            if (progress.mode == replication_mode::pipeline)
                progress.next_index = next + count;
            progress.sent(core.now, core.update);
            return {};
        }

        void reply_append(core_state& core, append_entries_result result) noexcept {
            const auto& follower = std::get<follower_state>(core.state);
            if (follower.leader_id == 0)
                return;
            (void)enqueue_message(core.update, follower.leader_id, follower.leader_address, result);
        }

        error_code set_leader(core_state& core, server_id id, string_view address) noexcept {
            try {
                auto& follower = std::get<follower_state>(core.state);
                follower.leader_address = address;
                follower.leader_id = id;
                return {};
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            }
        }

        error_code append_received(core_state& core, const append_entries& request,
                                   log_index& rejected, bool& asynchronous) noexcept {
            try {
                asynchronous = false;
                if (request.entries.size() >=
                    numeric_limits<log_index>::max() - request.prev_log_index)
                    return errc::malformed;
                if (request.prev_log_index != 0) {
                    const auto term = core.trail.term_of(request.prev_log_index);
                    if (term == 0)
                        return {};
                    if (term != request.prev_log_term) {
                        if (request.prev_log_index <= core.commit_index)
                            return errc::shutdown;
                        return {};
                    }
                } else if (request.prev_log_term != 0)
                    return errc::malformed;
                size_t first = 0;
                for (; first < request.entries.size(); ++first) {
                    const auto& entry = request.entries[first];
                    if (!entry || entry->term == 0)
                        return errc::malformed;
                    const auto index = request.prev_log_index + 1 + first;
                    const auto term = core.trail.term_of(index);
                    if (term != 0 && term != entry->term) {
                        if (index <= core.commit_index)
                            return errc::shutdown;
                        if (core.configuration_uncommitted_index >= index) {
                            if (auto error = rollback_configuration(core))
                                return error;
                        }
                        truncate_cached(core, index);
                        if (core.last_stored >= index)
                            core.last_stored = index - 1;
                        break;
                    }
                    if (term == 0)
                        break;
                }
                const auto index = request.prev_log_index + 1 + first;
                const bool changes_configuration = std::any_of(
                    request.entries.begin() + static_cast<ptrdiff_t>(first), request.entries.end(),
                    [](const auto& entry) { return entry && entry->type == entry_type::change; });
                append_transaction transaction{core, index, changes_configuration};
                size_t appended = 0;
                for (size_t i = first; i < request.entries.size(); ++i) {
                    if (!request.entries[i]) {
                        if (appended != 0)
                            truncate_cached(core, index);
                        return errc::malformed;
                    }
                    if (auto error = append_cached(core, *request.entries[i])) {
                        if (appended != 0)
                            truncate_cached(core, index);
                        return error;
                    }
                    ++appended;
                }
                auto& follower = std::get<follower_state>(core.state);
                const auto last_new = request.prev_log_index + request.entries.size();
                follower.match = std::max(follower.match, last_new);
                rejected = 0;
                if (request.leader_commit > core.commit_index) {
                    core.commit_index = std::min(request.leader_commit, last_new);
                    core.update.flags |= update_commit_index;
                }
                if (appended == 0) {
                    transaction.commit();
                    return {};
                }
                for (size_t i = first; i < request.entries.size(); ++i) {
                    if (request.entries[i]->type == entry_type::change) {
                        // Preserve the C core's first-new-entry index for a received batch.
                        if (auto error = uncommitted_change(core, index, *request.entries[i])) {
                            truncate_cached(core, index);
                            return error;
                        }
                    }
                }
                if (auto error = persist_entries(core, index, appended)) {
                    truncate_cached(core, index);
                    return error;
                }
                asynchronous = true;
                transaction.commit();
                return {};
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            }
        }

    }

    error_code replication_progress(core_state& core, size_t i) noexcept {
        auto leader = std::get_if<leader_state>(&core.state);
        if (leader == nullptr)
            return errc::not_leader;
        if (i >= core.configuration.size() || core.configuration.servers()[i].id == core.id)
            return errc::bad_id;
        auto& progress = leader->progress[i];
        const auto& server = core.configuration.servers()[i];
        auto previous = progress.next_index - 1;
        auto term = previous == 0 ? 0 : core.trail.term_of(previous);
        const bool missing = progress.next_index == 1 ? core.trail.term_of(1) == 0 : term == 0;
        bool heartbeat = false;
        if (missing || progress.mode == replication_mode::snapshot) {
            const auto snapshot_index = core.trail.snapshot_index();
            if (snapshot_index == 0)
                return errc::invalid;
            if (progress.mode != replication_mode::snapshot &&
                progress.online(core.now, core.election_timeout)) {
                progress.to_snapshot(snapshot_index);
                progress.snapshot.last_send = core.now;
                install_snapshot request;
                request.term = core.current_term;
                request.last_index = snapshot_index;
                request.last_term = core.trail.snapshot_term();
                request.configuration_index = core.configuration_last_snapshot_index;
                request.needs_data = true;
                if (auto error = enqueue_message(core.update, server.id, server.address,
                                                 std::move(request))) {
                    progress.abort_snapshot();
                    return error;
                }
                return {};
            }
            progress.next_index = snapshot_index + 1;
            previous = snapshot_index;
            term = core.trail.snapshot_term();
            heartbeat = true;
        }
        return send_append(core, i, previous, term, heartbeat);
    }

    void replication_heartbeat(core_state& core) noexcept {
        auto& leader = std::get<leader_state>(core.state);
        for (size_t i = 0; i < core.configuration.size(); ++i) {
            const auto& server = core.configuration.servers()[i];
            if (server.id == core.id ||
                (server.role == role::spare && server.id != leader.promotee_id))
                continue;
            if (!leader.progress[i].should_replicate(core.trail.last_index(), core.now,
                                                     core.heartbeat_timeout,
                                                     core.install_snapshot_timeout))
                continue;
            (void)replication_progress(core, i);
        }
    }

    void check_quorum(core_state& core, log_index index) noexcept {
        const auto& peers = std::get<leader_state>(core.state).progress;
        const auto half = core.configuration.voter_count() / 2;
        while (index > core.commit_index) {
            const auto term = core.trail.term_of(index);
            if (term == 0 || term < core.current_term)
                return;
            RAFTPP_ASSERT(term <= core.current_term);
            size_t votes = 0;
            for (size_t i = 0; i < core.configuration.size(); ++i) {
                if (core.configuration.servers()[i].role == role::voter &&
                    peers[i].match_index >= index)
                    ++votes;
            }
            if (votes > half) {
                core.commit_index = index;
                core.update.flags |= update_commit_index;
                return;
            }
            --index;
        }
    }

    error_code persisted_entries(core_state& core, log_index index) noexcept {
        if (index <= core.last_stored || index > core.trail.last_index())
            return errc::invalid;
        if (auto leader = std::get_if<leader_state>(&core.state)) {
            core.last_stored = index;
            const auto i = core.configuration.index_of(core.id);
            if (i < core.configuration.size())
                leader->progress[i].match_index = index;
            check_quorum(core, index);
            return {};
        }
        auto follower = std::get_if<follower_state>(&core.state);
        if (follower == nullptr)
            return errc::invalid;
        if (core.snapshot_installing)
            return {};
        core.last_stored = index;
        if (follower->match != 0) {
            append_entries_result result;
            result.term = core.current_term;
            result.features = feature_capacity;
            result.last_log_index = std::min(index, follower->match);
            result.capacity = core.capacity;
            reply_append(core, result);
        }
        return {};
    }

    error_code receive_append(core_state& core, server_id id, string_view address,
                              const append_entries& request) noexcept {
        append_entries_result result;
        result.features = feature_capacity;
        result.rejected = request.prev_log_index;
        const auto match = ensure_matching_terms(core, request.term);
        if (match >= 0) {
            if (std::holds_alternative<candidate_state>(core.state))
                convert_to_follower(core);
            if (!std::holds_alternative<follower_state>(core.state))
                return errc::invalid;
            if (auto error = set_leader(core, id, address))
                return error;
            core.election_timer_start = core.now;
            core.update.flags |= update_timeout;
            if (core.snapshot_installing && !request.entries.empty())
                return {};
            bool asynchronous = false;
            if (auto error = append_received(core, request, result.rejected, asynchronous)) {
                if (error != errc::busy)
                    return error;
            }
            if (asynchronous)
                return {};
            if (result.rejected != 0) {
                result.last_log_index = std::min(core.trail.last_index(), result.rejected - 1);
            } else {
                result.last_log_index = std::min<log_index>(
                    request.prev_log_index + request.entries.size(), core.last_stored);
            }
        }
        result.term = core.current_term;
        result.capacity = core.capacity;
        return enqueue_message(core.update, id, address, result);
    }

    error_code receive_append_result(core_state& core, server_id id,
                                     append_entries_result result) noexcept {
        if (!std::holds_alternative<leader_state>(core.state))
            return {};
        if (ensure_matching_terms(core, result.term) != 0)
            return {};
        const auto i = core.configuration.index_of(id);
        if (i == core.configuration.size())
            return {};
        auto& leader = std::get<leader_state>(core.state);
        auto& progress = leader.progress[i];
        progress.last_recv = core.now;
        progress.features = result.features;
        progress.capacity = result.capacity;
        if (result.rejected > 0) {
            // Compatibility with followers up to 0.19.1.
            if (result.last_log_index >= result.rejected)
                result.last_log_index = result.rejected - 1;
            if (progress.maybe_decrement(result.rejected, result.last_log_index))
                (void)replication_progress(core, i);
            return {};
        }
        const auto last_index = std::min(result.last_log_index, core.trail.last_index());
        if (!progress.maybe_update(last_index))
            return {};
        if (progress.mode == replication_mode::probe ||
            (progress.mode == replication_mode::snapshot && progress.snapshot_done()))
            progress.to_pipeline();
        if (leader.promotee_id != 0 && leader.promotee_id == id)
            update_catch_up(core);
        if (leader.transferee == id && progress.match_index == last_index && !leader.transferring) {
            if (transfer_start(core))
                leader.transferee = 0;
        }
        if (progress.mode == replication_mode::pipeline &&
            progress.should_replicate(core.trail.last_index(), core.now, core.heartbeat_timeout,
                                      core.install_snapshot_timeout))
            (void)replication_progress(core, i);
        check_quorum(core, last_index);
        return {};
    }

    error_code receive_snapshot(core_state& core, server_id id, string_view address,
                                const install_snapshot& request) noexcept {
        append_entries_result result;
        result.features = feature_capacity;
        if (ensure_matching_terms(core, request.term) >= 0) {
            if (std::holds_alternative<candidate_state>(core.state))
                convert_to_follower(core);
            if (!std::holds_alternative<follower_state>(core.state))
                return errc::invalid;
            if (auto error = set_leader(core, id, address))
                return error;
            core.election_timer_start = core.now;
            core.update.flags |= update_timeout;
            if (core.snapshot_installing)
                return {};
            if (request.last_index == 0 || request.last_index == numeric_limits<log_index>::max() ||
                request.last_term == 0 || request.needs_data)
                return errc::malformed;
            if (core.trail.snapshot_index() < request.last_index &&
                core.trail.term_of(request.last_index) != request.last_term) {
                try {
                    snapshot_metadata metadata{request.last_index, request.last_term,
                                               request.configuration, request.configuration_index};
                    auto configurations = pair{request.configuration, request.configuration};
                    if (auto error = core.trail.restore(metadata.index, metadata.term))
                        return error;
                    if (auto error = core.cache.restore(metadata.index, metadata.term))
                        return error;
                    core.last_stored = 0;
                    core.snapshot_installing = true;
                    core.snapshot_configuration_index = metadata.configuration_index;
                    core.snapshot_configurations.emplace(std::move(configurations));
                    core.update.snapshot.metadata = std::move(metadata);
                    core.update.snapshot.chunk = request.data;
                    core.update.snapshot.offset = 0;
                    core.update.snapshot.last = true;
                    core.update.flags |= update_snapshot;
                    return {};
                } catch (const bad_alloc&) {
                    return errc::out_of_memory;
                } catch (const length_error&) {
                    return errc::too_big;
                }
            }
            result.last_log_index = std::min(request.last_index, core.last_stored);
        }
        result.term = core.current_term;
        result.capacity = core.capacity;
        return enqueue_message(core.update, id, address, result);
    }

    error_code persisted_snapshot(core_state& core,
                                  const persisted_snapshot_event& event) noexcept {
        if (!std::holds_alternative<follower_state>(core.state) || !core.snapshot_installing ||
            !core.snapshot_configurations || event.metadata.index != core.trail.snapshot_index() ||
            event.metadata.term != core.trail.snapshot_term() ||
            event.metadata.configuration_index != core.snapshot_configuration_index)
            return errc::invalid;
        core.snapshot_installing = false;
        append_entries_result result;
        result.term = core.current_term;
        result.features = feature_capacity;
        auto& configurations = *core.snapshot_configurations;
        restore_snapshot_owned(core, event.metadata, std::move(configurations.first),
                               std::move(configurations.second));
        core.snapshot_configurations.reset();
        result.last_log_index = core.last_stored;
        result.capacity = core.capacity;
        reply_append(core, result);
        return {};
    }

}
