#ifndef RAFTPP_CORE_INTERNAL_HPP
#define RAFTPP_CORE_INTERNAL_HPP

#include <raftpp/detail/core.hpp>
#include "receive.hpp"
#include "election.hpp"
#include "tracing.hpp"
#include <algorithm>

namespace raftpp::detail {

    [[nodiscard]] error_code append_cached(core_state& core, const entry& entry) noexcept;
    void truncate_cached(core_state& core, log_index index) noexcept;

    // Roll back an incomplete append as one domain operation. Backups use
    // standard owned values; no cleanup path allocates or calls user code.
    class append_transaction {
    public:
        append_transaction(core_state& core, log_index first, bool changes_configuration)
            : core_(core), first_(first), commit_(core.commit_index),
              uncommitted_(core.configuration_uncommitted_index), flags_(core.update.flags) {
            if (auto follower = std::get_if<follower_state>(&core.state))
                match_ = follower->match;
            if (changes_configuration) {
                configuration_.emplace(core.configuration);
                if (auto leader = std::get_if<leader_state>(&core.state))
                    progress_.emplace(leader->progress);
            }
        }
        append_transaction(const append_transaction&) = delete;
        append_transaction& operator=(const append_transaction&) = delete;
        ~append_transaction() {
            if (committed_)
                return;
            if (core_.trail.has_entry(first_))
                truncate_cached(core_, first_);
            if (configuration_)
                core_.configuration = std::move(*configuration_);
            if (progress_)
                std::get<leader_state>(core_.state).progress = std::move(*progress_);
            core_.configuration_uncommitted_index = uncommitted_;
            core_.commit_index = commit_;
            core_.update.flags = flags_;
            if (auto follower = std::get_if<follower_state>(&core_.state)) {
                follower->match = std::min(match_, core_.trail.last_index());
            }
        }
        void commit() noexcept {
            committed_ = true;
        }

    private:
        core_state& core_;
        log_index first_, commit_, uncommitted_, match_ = 0;
        unsigned flags_;
        optional<configuration> configuration_;
        optional<resource_vector<progress>> progress_;
        bool committed_ = false;
    };
    [[nodiscard]] error_code persist_entries(core_state& core, log_index index,
                                             size_t count) noexcept;
    [[nodiscard]] error_code uncommitted_change(core_state& core, log_index index,
                                                const entry& entry) noexcept;
    [[nodiscard]] error_code rollback_configuration(core_state& core) noexcept;
    [[nodiscard]] error_code restore_snapshot(core_state& core,
                                              const snapshot_metadata& metadata) noexcept;
    void restore_snapshot_owned(core_state& core, const snapshot_metadata& metadata,
                                configuration current, configuration committed) noexcept;
    [[nodiscard]] error_code apply_configuration(core_state& core,
                                                 const configuration_event& event) noexcept;
    [[nodiscard]] error_code take_snapshot(core_state& core, const snapshot_event& event) noexcept;
    [[nodiscard]] error_code transfer_start(core_state& core) noexcept;
    [[nodiscard]] error_code transfer_leadership(core_state& core, server_id id) noexcept;
    [[nodiscard]] error_code catch_up(core_state& core, server_id id) noexcept;
    void update_catch_up(core_state& core) noexcept;
    void check_quorum(core_state& core, log_index index) noexcept;
    [[nodiscard]] error_code persisted_entries(core_state& core, log_index index) noexcept;
    [[nodiscard]] error_code persisted_snapshot(core_state& core,
                                                const persisted_snapshot_event& event) noexcept;
    [[nodiscard]] error_code receive_append(core_state& core, server_id id, string_view address,
                                            const append_entries& request) noexcept;
    [[nodiscard]] error_code receive_append_result(core_state& core, server_id id,
                                                   append_entries_result result) noexcept;
    [[nodiscard]] error_code receive_snapshot(core_state& core, server_id id, string_view address,
                                              const install_snapshot& request) noexcept;
    [[nodiscard]] error_code timeout(core_state& core) noexcept;

}

#endif // RAFTPP_CORE_INTERNAL_HPP
