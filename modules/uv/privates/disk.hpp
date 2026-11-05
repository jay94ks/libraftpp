#ifndef RAFTPP_DETAIL_DISK_HPP
#define RAFTPP_DETAIL_DISK_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/encoding.hpp>
#include <raftpp/io.hpp>
#include "file.hpp"
#include "probe.hpp"
#include <raftpp/config.hpp>

#if RAFTPP_WITH_UV
namespace raftpp::detail {

    enum class file_operation {
        open,
        read,
        write,
        allocate,
        sync_file,
        sync_data,
        close,
        sync_directory,
        publish_metadata,
        publish_snapshot,
        remove,
        publish_segment,
        truncate,
        rename,
        scan
    };
    // Internal syscall-boundary hook for diagnostics and failure-injection tests.
    using file_hook = function<error_code(file_operation, string_view)>;
    struct disk_state;
    struct prepared_snapshot_state;

    struct snapshot_file {
        raftpp::term term = 0;
        log_index index = 0;
        uint64_t timestamp = 0;
        string filename; // Metadata filename; data name omits .meta.
    };
    struct segment_file {
        bool is_open = false;
        log_index first_index = 0, end_index = 0;
        uint64_t counter = 0;
        string filename;
    };
    // Syntax failure preserves output; assigning the owning filename can throw.
    [[nodiscard]] RAFTPP_UV_API bool parse_snapshot_filename(string_view name, bool metadata,
                                                             snapshot_file& output);
    [[nodiscard]] RAFTPP_UV_API bool parse_segment_filename(string_view name, segment_file& output);

    class RAFTPP_UV_API prepared_snapshot {
    public:
        prepared_snapshot() noexcept;
        ~prepared_snapshot();
        prepared_snapshot(prepared_snapshot&&) noexcept;
        prepared_snapshot& operator=(prepared_snapshot&&) noexcept;
        prepared_snapshot(const prepared_snapshot&) = delete;
        prepared_snapshot& operator=(const prepared_snapshot&) = delete;
        [[nodiscard]] bool empty() const noexcept {
            return !state_;
        }

    private:
        resource_owned<prepared_snapshot_state> state_;
        friend class disk_storage;
    };

    // Synchronous durable file operations. The UV coordinator schedules worker
    // phases and barriers; no event loop or node callbacks run in this class.
    class RAFTPP_UV_API disk_storage {
    public:
        explicit disk_storage(fs::path directory, file_hook hook = {});
        ~disk_storage();
        disk_storage(const disk_storage&) = delete;
        disk_storage& operator=(const disk_storage&) = delete;
        disk_storage(disk_storage&&) noexcept;
        disk_storage& operator=(disk_storage&&) noexcept;

        [[nodiscard]] error_code open(error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] persistent_metadata metadata() const noexcept;
        [[nodiscard]] error_code set_term(raftpp::term value,
                                          error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code set_vote(server_id value,
                                          error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code prepare_snapshot(const snapshot& value, uint64_t timestamp,
                                                  prepared_snapshot& output,
                                                  error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code commit_snapshot(prepared_snapshot& value,
                                                 error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code list_snapshots(vector<snapshot_file>& output,
                                                error_info* diagnostic = nullptr) const noexcept;
        [[nodiscard]] error_code load_snapshot(const snapshot_file& file, snapshot& output,
                                               size_t max_size = numeric_limits<size_t>::max(),
                                               error_info* diagnostic = nullptr) const noexcept;
        [[nodiscard]] error_code latest_snapshot(optional<snapshot>& output,
                                                 size_t max_size = numeric_limits<size_t>::max(),
                                                 error_info* diagnostic = nullptr) const noexcept;
        [[nodiscard]] error_code prune_snapshots(error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code list_segments(vector<segment_file>& output,
                                               error_info* diagnostic = nullptr) const noexcept;
        [[nodiscard]] error_code load_closed(const segment_file& file, vector<entry>& output,
                                             error_info* diagnostic = nullptr) const noexcept;
        [[nodiscard]] error_code write_segment(log_index first_index, view<const entry> entries,
                                               error_info* diagnostic = nullptr) noexcept;
        // Recovery may rename/remove files; the output value changes only on success.
        [[nodiscard]] error_code load(io_state& output, bool auto_recovery = true,
                                      size_t max_snapshot_size = numeric_limits<size_t>::max(),
                                      error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code bootstrap(const configuration& configuration,
                                           size_t block_size = 4096,
                                           error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code recover(const configuration& configuration,
                                         size_t block_size = 4096, bool auto_recovery = true,
                                         error_info* diagnostic = nullptr) noexcept;
        // The coordinator must finalize active writers before truncating.
        [[nodiscard]] error_code truncate(log_index index,
                                          error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code prune_segments(log_index last_index, size_t trailing,
                                                error_info* diagnostic = nullptr,
                                                bool local = false) noexcept;
        [[nodiscard]] error_code prepare_segment(uint64_t counter, size_t size, file_handle& output,
                                                 error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code finalize_segment(uint64_t counter, size_t used, log_index first,
                                                  log_index last,
                                                  error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code probe(disk_capabilities& output, writer_hook hook = {},
                                       error_info* diagnostic = nullptr) const noexcept;

    private:
        shared<disk_state> state_;
    };

}
#endif // RAFTPP_WITH_UV
#endif // RAFTPP_DETAIL_DISK_HPP
