// C++ port of cowsql/raft src/progress.c; see LICENSE.md.
#include <raftpp/detail/progress.hpp>

#include <algorithm>
#include "assert.hpp"

namespace raftpp::detail {

    bool progress::up_to_date(log_index last_index) const noexcept {
        return next_index == last_index + 1;
    }

    bool progress::online(time now, unsigned election_timeout) const noexcept {
        if (last_recv == never)
            return false;
        RAFTPP_ASSERT(now >= last_recv);
        return now - last_recv < election_timeout;
    }

    bool progress::contacted_recently(time election_timer_start) const noexcept {
        return last_recv != never && last_recv >= election_timer_start;
    }

    bool progress::should_replicate(log_index last_index, time now, unsigned heartbeat_timeout,
                                    unsigned install_snapshot_timeout) noexcept {
        RAFTPP_ASSERT(next_index <= last_index + 1);
        RAFTPP_ASSERT(last_send == never || last_send <= now);
        const bool heartbeat = last_send == never || now - last_send >= heartbeat_timeout;
        switch (mode) {
        case replication_mode::snapshot:
            RAFTPP_ASSERT(snapshot.last_send != never);
            if (now - snapshot.last_send >= install_snapshot_timeout) {
                abort_snapshot();
                return true;
            }
            return heartbeat;
        case replication_mode::probe:
            return heartbeat;
        case replication_mode::pipeline:
            return !up_to_date(last_index) || heartbeat;
        }
        RAFTPP_ASSERT(false);
        return false;
    }

    void progress::sent(time now, core_update& update) noexcept {
        last_send = now;
        update.flags |= update_timeout;
    }

    time progress::last_message_sent() const noexcept {
        return snapshot.last_send != never && snapshot.last_send > last_send ? snapshot.last_send
                                                                             : last_send;
    }

    void progress::to_snapshot(log_index index) noexcept {
        mode = replication_mode::snapshot;
        snapshot.index = index;
        next_index = index + 1;
    }

    void progress::abort_snapshot() noexcept {
        next_index = match_index + 1;
        snapshot.index = 0;
        mode = replication_mode::probe;
    }

    void progress::to_probe() noexcept {
        if (mode == replication_mode::snapshot) {
            RAFTPP_ASSERT(snapshot.index > 0);
            snapshot.index = 0;
        } else {
            next_index = match_index + 1;
        }
        mode = replication_mode::probe;
    }

    string_view progress::state_name() const noexcept {
        switch (mode) {
        case replication_mode::probe:
            return "probe";
        case replication_mode::pipeline:
            return "pipeline";
        case replication_mode::snapshot:
            return "snapshot";
        }
        RAFTPP_ASSERT(false);
        return {};
    }

    bool progress::maybe_decrement(log_index rejected, log_index last_index) noexcept {
        RAFTPP_ASSERT(rejected > 0 && last_index < rejected);
        RAFTPP_ASSERT(next_index > 0 && match_index < next_index);
        if (mode == replication_mode::snapshot) {
            if (rejected != snapshot.index)
                return false;
            abort_snapshot();
            return true;
        }
        if (mode == replication_mode::pipeline) {
            if (rejected <= match_index)
                return false;
            next_index = std::min(rejected, match_index + 1);
            to_probe();
            return true;
        }
        RAFTPP_ASSERT(mode == replication_mode::probe);
        if (rejected != next_index - 1)
            return false;
        next_index = std::min(rejected, last_index + 1);
        RAFTPP_ASSERT(match_index < next_index);
        return true;
    }

    bool progress::maybe_update(log_index last_index) noexcept {
        const bool updated = match_index < last_index;
        if (updated)
            match_index = last_index;
        next_index = std::max(next_index, last_index + 1);
        return updated;
    }

    bool progress::snapshot_done() const noexcept {
        RAFTPP_ASSERT(mode == replication_mode::snapshot);
        return match_index >= snapshot.index;
    }

    void progress::catch_up_start() noexcept {
        RAFTPP_ASSERT(catch_up != catch_up_status::running);
        catch_up = catch_up_status::running;
    }

    void progress::catch_up_abort() noexcept {
        RAFTPP_ASSERT(catch_up == catch_up_status::running);
        catch_up = catch_up_status::aborted;
    }

    void progress::catch_up_finish() noexcept {
        RAFTPP_ASSERT(catch_up == catch_up_status::running);
        catch_up = catch_up_status::finished;
    }

    template <typename Allocator>
    static error_code build_progress_values(const configuration& configuration, server_id self,
                                            log_index last_index, log_index last_stored,
                                            vector<progress, Allocator>& output) noexcept {
        if (last_index == numeric_limits<log_index>::max())
            return errc::too_big;
        try {
            vector<progress, Allocator> built(configuration.size(), progress{last_index},
                                              output.get_allocator());
            const auto i = configuration.index_of(self);
            if (i < built.size())
                built[i].match_index = last_stored;
            output.swap(built);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    template <typename Allocator>
    static error_code rebuild_progress_values(const configuration& current,
                                              const configuration& next, log_index last_index,
                                              vector<progress, Allocator>& output) noexcept {
        if (output.size() != current.size())
            return errc::invalid;
        if (last_index == numeric_limits<log_index>::max())
            return errc::too_big;
        try {
            vector<progress, Allocator> rebuilt{output.get_allocator()};
            rebuilt.reserve(next.size());
            for (const auto& server : next.servers()) {
                const auto i = current.index_of(server.id);
                rebuilt.push_back(i < current.size() ? output[i] : progress{last_index});
            }
            output.swap(rebuilt);
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code build_progress(const configuration& configuration, server_id self,
                              log_index last_index, log_index last_stored,
                              vector<progress>& output) noexcept {
        return build_progress_values(configuration, self, last_index, last_stored, output);
    }

    error_code build_progress(const configuration& configuration, server_id self,
                              log_index last_index, log_index last_stored,
                              resource_vector<progress>& output) noexcept {
        return build_progress_values(configuration, self, last_index, last_stored, output);
    }

    error_code rebuild_progress(const configuration& current, const configuration& next,
                                log_index last_index, vector<progress>& output) noexcept {
        return rebuild_progress_values(current, next, last_index, output);
    }

    error_code rebuild_progress(const configuration& current, const configuration& next,
                                log_index last_index, resource_vector<progress>& output) noexcept {
        return rebuild_progress_values(current, next, last_index, output);
    }

}
