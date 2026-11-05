// C++ port of cowsql/raft persistent file operations; see LICENSE.md.
#include "disk.hpp"
#include <raftpp/uv/compression.hpp>
#include "byte.hpp"
#include "file.hpp"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <uv.h>

namespace raftpp::detail {

    namespace {
        struct fs_request {
            uv_fs_t value{};
            ~fs_request() {
                uv_fs_req_cleanup(&value);
            }
        };
        template <typename F>
        error_code guarded(F&& action, error_info* diagnostic = nullptr) noexcept {
            try {
                return action();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (const fs::filesystem_error& error) {
                if (diagnostic)
                    *diagnostic = make_error_info(errc::io_error, "filesystem",
                                                  error.path1().native(), error.code());
                return errc::io_error;
            } catch (...) {
                return errc::io_error;
            }
        }
        template <typename F> error_code guarded_cleanup(F&& action) noexcept {
            return guarded(std::forward<F>(action), nullptr);
        }
        template <typename F>
        error_code capture_diagnostic(F&& action, error_info* output, string_view operation,
                                      const fs::path* directory) noexcept {
            error_info captured;
            const auto status = guarded([&] { return action(&captured); }, &captured);
            if (output)
                *output = !status ? error_info{}
                          : captured.code == status
                              ? captured
                              : make_error_info(status, operation,
                                                directory ? string_view{directory->native()}
                                                          : string_view{});
            return status;
        }
        bool snapshot_before(const snapshot_file& a, const snapshot_file& b) noexcept {
            if (a.term != b.term)
                return a.term < b.term;
            if (a.index != b.index)
                return a.index < b.index;
            return a.timestamp < b.timestamp;
        }
        bool segment_before(const segment_file& a, const segment_file& b) noexcept {
            if (a.is_open != b.is_open)
                return !a.is_open;
            if (a.is_open) {
                if (a.counter != b.counter)
                    return a.counter < b.counter;
            } else {
                if (a.first_index != b.first_index)
                    return a.first_index < b.first_index;
                if (a.end_index != b.end_index)
                    return a.end_index < b.end_index;
            }
            return a.filename < b.filename;
        }
        error_code closed_filename(log_index first, log_index last, string& output) {
            if (first == 0 || last < first)
                return errc::invalid;
            // The reference format and filename buffer allow 16 digits per index.
            if (last > 9999999999999999ULL)
                return errc::too_big;
            auto a = std::to_string(first), b = std::to_string(last);
            output = string(16 - a.size(), '0') + a + "-" + string(16 - b.size(), '0') + b;
            return {};
        }
    }

    struct disk_state {
        fs::path directory;
        file_hook hook;
        persistent_metadata metadata;
        bool opened = false, metadata_failed = false;

        error_code note(error_info* diagnostic, error_code status, string_view operation,
                        string_view name = {}, error_code cause = {}) const noexcept {
            if (diagnostic) {
                array<char, 256> resource{};
                std::snprintf(resource.data(), resource.size(), "%s%s%.*s", directory.c_str(),
                              name.empty() ? "" : "/",
                              static_cast<int>(std::min(
                                  name.size(), static_cast<size_t>(numeric_limits<int>::max()))),
                              name.empty() ? "" : name.data());
                *diagnostic = make_error_info(status, operation, resource.data(), cause);
            }
            return status;
        }
        error_code native(error_info* diagnostic, string_view operation, string_view name,
                          int64_t result, errc status = errc::io_error) const noexcept {
            const auto cause =
                result < 0 && result >= -static_cast<int64_t>(numeric_limits<int>::max())
                    ? error_code{static_cast<int>(-result), std::generic_category()}
                    : error_code{};
            return note(diagnostic, status, operation, name, cause);
        }
        error_code point(file_operation operation, string_view filename,
                         error_info* diagnostic) const noexcept {
            const auto status = hook
                                    ? guarded([&] { return hook(operation, filename); }, diagnostic)
                                    : error_code{};
            if (status) {
                static constexpr array<string_view, 15> names{"open",
                                                              "read",
                                                              "write",
                                                              "allocate",
                                                              "fsync",
                                                              "fdatasync",
                                                              "close",
                                                              "fsync_directory",
                                                              "publish_metadata",
                                                              "publish_snapshot",
                                                              "unlink",
                                                              "publish_segment",
                                                              "truncate",
                                                              "rename",
                                                              "scandir"};
                const auto index = static_cast<size_t>(operation);
                const auto cause =
                    diagnostic && diagnostic->code == status ? diagnostic->cause : error_code{};
                return note(diagnostic, status,
                            index < names.size() ? names[index] : string_view{"disk.hook"},
                            filename, cause);
            }
            return {};
        }
        string path(string_view filename) const {
            return (directory / fs::path{filename}).native();
        }
        error_code close(file_handle& file, string_view filename,
                         error_info* diagnostic) const noexcept {
            const auto injected = point(file_operation::close, filename, diagnostic);
            error_info info;
            const auto actual = file.close(&info);
            if (injected)
                return injected;
            return actual ? note(diagnostic, actual, "disk.close", filename, info.cause)
                          : error_code{};
        }
        error_code open_file(const string& path, string_view name, int flags, int mode,
                             file_handle& file, error_info* diagnostic) const {
            if (auto error = point(file_operation::open, name, diagnostic))
                return error;
            fs_request request;
            const auto fd =
                uv_fs_open(nullptr, &request.value, path.c_str(), flags | O_CLOEXEC, mode, nullptr);
            if (fd < 0)
                return native(diagnostic, "disk.open", name, fd);
            file = file_handle{fd};
            return {};
        }
        error_code sync_directory(error_info* diagnostic) const noexcept {
            if (auto error = point(file_operation::sync_directory, {}, diagnostic))
                return error;
            file_handle file;
            fs_request opened;
            const auto fd = uv_fs_open(nullptr, &opened.value, directory.c_str(),
                                       O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0, nullptr);
            if (fd < 0)
                return native(diagnostic, "disk.open_directory", {}, fd);
            file = file_handle{fd};
            fs_request synced;
            const auto status = uv_fs_fsync(nullptr, &synced.value, file.get(), nullptr);
            if (status < 0)
                return native(diagnostic, "disk.fsync_directory", {}, status);
            error_info info;
            const auto closed = file.close(&info);
            return closed ? note(diagnostic, closed, "disk.close_directory", {}, info.cause)
                          : error_code{};
        }
        error_code sync(file_handle& file, string_view name, bool data_only,
                        error_info* diagnostic) const noexcept {
            if (auto error =
                    point(data_only ? file_operation::sync_data : file_operation::sync_file, name,
                          diagnostic))
                return error;
            fs_request request;
            const auto status = data_only
                                    ? uv_fs_fdatasync(nullptr, &request.value, file.get(), nullptr)
                                    : uv_fs_fsync(nullptr, &request.value, file.get(), nullptr);
            return status < 0 ? native(diagnostic, data_only ? "disk.fdatasync" : "disk.fsync",
                                       name, status)
                              : error_code{};
        }
        error_code write(file_handle& file, string_view name, view<const shared_buffer> data,
                         error_info* diagnostic) const noexcept {
            if (auto error = point(file_operation::write, name, diagnostic))
                return error;
            int64_t offset = 0;
            for (const auto& part : data) {
                size_t position = 0;
                while (position < part.size()) {
                    const auto count =
                        std::min<size_t>(part.size() - position, numeric_limits<int>::max());
                    if (count > static_cast<uint64_t>(numeric_limits<int64_t>::max() - offset))
                        return errc::too_big;
                    uv_buf_t buffer{
                        reinterpret_cast<char*>(const_cast<byte*>(part.bytes().data() + position)),
                        count};
                    fs_request request;
                    const auto written = uv_fs_write(nullptr, &request.value, file.get(), &buffer,
                                                     1, offset, nullptr);
                    if (written <= 0)
                        return native(diagnostic, "disk.write", name, written);
                    position += static_cast<size_t>(written);
                    offset += written;
                }
            }
            return {};
        }
        error_code read(string_view name, buffer& output, size_t maximum, size_t exact,
                        error_info* diagnostic) const {
            const auto full_path = path(name);
            fs_request stat;
            const auto stated = uv_fs_stat(nullptr, &stat.value, full_path.c_str(), nullptr);
            if (stated < 0)
                return native(diagnostic, "disk.stat", name, stated);
            if (exact > stat.value.statbuf.st_size)
                return note(diagnostic, errc::io_error, "disk.short_read", name);
            const auto size =
                exact != 0 ? static_cast<uint64_t>(exact) : stat.value.statbuf.st_size;
            if (size > maximum || size > numeric_limits<size_t>::max() ||
                size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
                return errc::too_big;
            buffer result{static_cast<size_t>(size)};
            file_handle file;
            if (auto error = open_file(full_path, name, O_RDONLY, 0, file, diagnostic))
                return error;
            if (auto error = point(file_operation::read, name, diagnostic))
                return error;
            size_t offset = 0;
            while (offset < result.size()) {
                const auto count =
                    std::min<size_t>(result.size() - offset, numeric_limits<int>::max());
                uv_buf_t data{reinterpret_cast<char*>(result.data() + offset), count};
                fs_request request;
                const auto got = uv_fs_read(nullptr, &request.value, file.get(), &data, 1,
                                            static_cast<int64_t>(offset), nullptr);
                if (got <= 0)
                    return native(diagnostic, "disk.read", name, got);
                offset += static_cast<size_t>(got);
            }
            if (auto error = close(file, name, diagnostic))
                return error;
            output = std::move(result);
            return {};
        }
        error_code remove(string_view name, error_info* diagnostic) const {
            if (auto error = point(file_operation::remove, name, diagnostic))
                return error;
            const auto full_path = path(name);
            fs_request request;
            const auto status = uv_fs_unlink(nullptr, &request.value, full_path.c_str(), nullptr);
            return status < 0 ? native(diagnostic, "disk.unlink", name, status) : error_code{};
        }
        error_code filenames(vector<string>& output, error_info* diagnostic) const {
            if (auto error = point(file_operation::scan, {}, diagnostic))
                return error;
            fs_request request;
            // libstdc++ directory_iterator can terminate on bad_alloc in its
            // internal noexcept path. Use libuv's owned scan request to preserve
            // our error-return contract, keeping names and paths standard values.
            const auto status =
                uv_fs_scandir(nullptr, &request.value, directory.c_str(), 0, nullptr);
            if (status < 0)
                return native(diagnostic, "disk.scandir", {}, status,
                              status == UV_ENOMEM ? errc::out_of_memory : errc::io_error);
            vector<string> result;
            uv_dirent_t entry{};
            for (;;) {
                const auto next = uv_fs_scandir_next(&request.value, &entry);
                if (next == UV_EOF)
                    break;
                if (next < 0)
                    return native(diagnostic, "disk.scandir.next", {}, next,
                                  next == UV_ENOMEM ? errc::out_of_memory : errc::io_error);
                result.emplace_back(entry.name);
            }
            output = std::move(result);
            return {};
        }
        error_code rename(string_view from, string_view to, error_info* diagnostic) const {
            const auto old_path = path(from), new_path = path(to);
            if (auto error = point(file_operation::rename, from, diagnostic))
                return error;
            fs_request request;
            const auto status =
                uv_fs_rename(nullptr, &request.value, old_path.c_str(), new_path.c_str(), nullptr);
            return status < 0 ? native(diagnostic, "disk.rename", from, status) : error_code{};
        }
        error_code finalize(string_view from, string_view to, size_t size,
                            error_info* diagnostic) const {
            // Allocate names before altering the file. No active writer may own it.
            const auto old_path = path(from), new_path = path(to);
            if (fs::exists(new_path))
                return note(diagnostic, errc::io_error, "disk.destination_exists", to);
            if (size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
                return errc::too_big;
            file_handle file;
            if (auto error = open_file(old_path, from, O_RDWR, 0, file, diagnostic))
                return error;
            if (auto error = point(file_operation::truncate, from, diagnostic))
                return error;
            fs_request request;
            const auto truncated = uv_fs_ftruncate(nullptr, &request.value, file.get(),
                                                   static_cast<int64_t>(size), nullptr);
            if (truncated < 0)
                return native(diagnostic, "disk.truncate", from, truncated);
            if (auto error = sync(file, from, false, diagnostic))
                return error;
            if (auto error = close(file, from, diagnostic))
                return error;
            if (auto error = point(file_operation::rename, from, diagnostic))
                return error;
            fs_request renamed;
            const auto renamed_status =
                uv_fs_rename(nullptr, &renamed.value, old_path.c_str(), new_path.c_str(), nullptr);
            if (renamed_status < 0)
                return native(diagnostic, "disk.rename", from, renamed_status);
            return sync_directory(diagnostic);
        }
        error_code make_file(string_view name, view<const shared_buffer> data,
                             error_info* diagnostic) const {
            const auto temporary_name = "tmp-" + string{name};
            const auto temporary_path = path(temporary_name), destination = path(name);
            file_handle file;
            bool created = false, published = false;
            const auto status = guarded(
                [&]() -> error_code {
                    if (auto error = open_file(temporary_path, temporary_name,
                                               O_WRONLY | O_CREAT | O_EXCL, 0600, file, diagnostic))
                        return error;
                    created = true;
                    if (auto error = write(file, temporary_name, data, diagnostic))
                        return error;
                    if (auto error = sync(file, temporary_name, false, diagnostic))
                        return error;
                    if (auto error = close(file, temporary_name, diagnostic))
                        return error;
                    if (fs::exists(destination))
                        return note(diagnostic, errc::io_error, "disk.destination_exists", name);
                    if (auto error = point(file_operation::publish_segment, name, diagnostic))
                        return error;
                    fs_request request;
                    const auto published_status =
                        uv_fs_rename(nullptr, &request.value, temporary_path.c_str(),
                                     destination.c_str(), nullptr);
                    if (published_status < 0)
                        return native(diagnostic, "disk.rename", name, published_status);
                    published = true;
                    return sync_directory(diagnostic);
                },
                diagnostic);
            // Match UvFsMakeFile: do not retain a new file after sync failure.
            if (status && created)
                (void)guarded_cleanup([&] {
                    return remove(published ? name : string_view{temporary_name}, nullptr);
                });
            return status;
        }
        error_code read_segment(const segment_file& file, log_index first, vector<entry>& output,
                                size_t& used, error_info* diagnostic) const {
            buffer raw;
            if (auto error = read(file.filename, raw, numeric_limits<size_t>::max(), 0, diagnostic))
                return error;
            if (raw.empty()) {
                if (!file.is_open)
                    return errc::corrupt;
                output.clear();
                used = 0;
                return {};
            }
            if (raw.size() < 8)
                return errc::io_error;
            const auto format = load_little_endian<uint64_t>(raw.bytes().first<8>());
            if (format != disk_format_version) {
                if (!file.is_open || format != 0 ||
                    !std::ranges::all_of(raw.bytes(), [](byte b) { return b == byte{}; }))
                    return errc::corrupt;
                output.clear();
                used = 0;
                return {};
            }
            shared_buffer data{std::move(raw)};
            vector<entry> result;
            size_t offset = 8;
            encoding_limits limits{numeric_limits<size_t>::max(), numeric_limits<size_t>::max(),
                                   262144};
            do {
                vector<entry> batch;
                size_t consumed = 0;
                const auto status = decode_record(data.slice(offset), batch, consumed, limits);
                if (status) {
                    // Only corrupt batches are a recoverable open-file tail.
                    // Short I/O, malformed entry types and OOM still fail.
                    if (file.is_open && status == errc::corrupt)
                        break;
                    return status;
                }
                if (batch.size() > numeric_limits<log_index>::max() - first - result.size())
                    return errc::too_big;
                result.insert(result.end(), std::make_move_iterator(batch.begin()),
                              std::make_move_iterator(batch.end()));
                offset += consumed;
            } while (offset < data.size());
            if (!file.is_open && result.size() != file.end_index - file.first_index + 1)
                return errc::corrupt;
            used = result.empty() ? 0 : offset;
            output = std::move(result);
            return {};
        }
        error_code load_slot(unsigned slot, persistent_metadata& output, error_info* diagnostic) {
            const auto name = "metadata" + std::to_string(slot);
            const auto full_path = path(name);
            fs_request stat;
            const auto status = uv_fs_stat(nullptr, &stat.value, full_path.c_str(), nullptr);
            if (status == UV_ENOENT) {
                output = {};
                return {};
            }
            if (status < 0)
                return native(diagnostic, "disk.metadata.stat", name, status);
            if (stat.value.statbuf.st_size < 32) {
                if (auto error = remove(name, diagnostic))
                    return error;
                output = {};
                return {};
            }
            if (stat.value.statbuf.st_size > 32)
                return errc::corrupt;
            buffer data;
            if (auto error = read(name, data, 32, 0, diagnostic))
                return error;
            return decode_metadata(data.bytes(), output);
        }
        error_code store_metadata(persistent_metadata value, error_info* diagnostic) {
            const auto name = value.version % 2 ? string{"metadata1"} : string{"metadata2"};
            const auto full_path = path(name);
            buffer encoded;
            if (auto error = encode_metadata(value, encoded))
                return error;
            array<shared_buffer, 1> data{shared_buffer{std::move(encoded)}};
            file_handle file;
            if (auto error = point(file_operation::open, name, diagnostic))
                return error;
            fs_request opened;
            auto fd = uv_fs_open(nullptr, &opened.value, full_path.c_str(), O_WRONLY | O_CLOEXEC, 0,
                                 nullptr);
            const bool existing = fd != UV_ENOENT;
            if (fd == UV_ENOENT) {
                fs_request created;
                fd = uv_fs_open(nullptr, &created.value, full_path.c_str(),
                                O_WRONLY | O_CREAT | O_CLOEXEC, 0600, nullptr);
            }
            if (fd < 0)
                return native(diagnostic, "disk.metadata.open", name, fd);
            file = file_handle{fd};
            // An incomplete or unsynchronized write must never be followed by
            // overwriting the other slot in the same live storage instance.
            metadata_failed = true;
            if (auto error = write(file, name, data, diagnostic))
                return error;
            if (auto error = sync(file, name, existing, diagnostic))
                return error;
            if (auto error = close(file, name, diagnostic))
                return error;
            if (!existing)
                if (auto error = sync_directory(diagnostic))
                    return error;
            metadata = value;
            metadata_failed = false;
            return {};
        }
        error_code temporary(string_view name, view<const shared_buffer> data, file_handle& output,
                             error_info* diagnostic) {
            uint64_t size = 0;
            for (const auto& part : data) {
                if (part.size() > static_cast<uint64_t>(numeric_limits<int64_t>::max()) - size)
                    return errc::too_big;
                size += part.size();
            }
            if (size == 0)
                return errc::invalid;
            file_handle file;
            if (auto error = open_file(directory.native(), name, O_TMPFILE | O_WRONLY, 0600, file,
                                       diagnostic))
                return error;
            if (auto error = allocate(file, name, static_cast<size_t>(size), diagnostic))
                return error;
            if (auto error = write(file, name, data, diagnostic))
                return error;
            if (auto error = sync(file, name, false, diagnostic))
                return error;
            output = std::move(file);
            return {};
        }
        error_code allocate(file_handle& file, string_view name, size_t size,
                            error_info* diagnostic) {
            if (auto error = point(file_operation::allocate, name, diagnostic))
                return error;
            error_info info;
            const auto status = file.allocate(size, &info);
            return status ? note(diagnostic, status, "disk.allocate", name, info.cause)
                          : error_code{};
        }
    };

    struct prepared_snapshot_state {
        shared<disk_state> disk;
        file_handle metadata, data;
        string metadata_name, data_name, metadata_path, data_path, metadata_proc, data_proc;
    };

    bool parse_snapshot_filename(string_view name, bool metadata, snapshot_file& output) {
        if (!name.starts_with("snapshot-"))
            return false;
        const auto full = name;
        name.remove_prefix(9);
        if (metadata) {
            if (!name.ends_with(".meta"))
                return false;
            name.remove_suffix(5);
        }
        snapshot_file result;
        auto number = [&](uint64_t& value, bool last) {
            const auto end = last ? name.size() : name.find('-');
            if (end == string_view::npos || end == 0)
                return false;
            const auto parsed = std::from_chars(name.data(), name.data() + end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != name.data() + end)
                return false;
            name.remove_prefix(end);
            if (!last)
                name.remove_prefix(1);
            return true;
        };
        if (!number(result.term, false) || !number(result.index, false) ||
            !number(result.timestamp, true) || !name.empty())
            return false;
        result.filename = full;
        output = std::move(result);
        return true;
    }

    bool parse_segment_filename(string_view name, segment_file& output) {
        if (name.size() >= 34)
            return false;
        const auto full = name;
        segment_file result;
        auto number = [](string_view text, uint64_t& value) {
            if (text.empty())
                return false;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        };
        if (name.starts_with("open-")) {
            result.is_open = true;
            if (!number(name.substr(5), result.counter))
                return false;
        } else {
            const auto separator = name.find('-');
            if (separator == string_view::npos || separator > 16 ||
                name.size() - separator - 1 > 16 ||
                !number(name.substr(0, separator), result.first_index) ||
                !number(name.substr(separator + 1), result.end_index))
                return false;
        }
        result.filename = full;
        output = std::move(result);
        return true;
    }

    prepared_snapshot::prepared_snapshot() noexcept = default;
    prepared_snapshot::~prepared_snapshot() = default;
    prepared_snapshot::prepared_snapshot(prepared_snapshot&&) noexcept = default;
    prepared_snapshot& prepared_snapshot::operator=(prepared_snapshot&&) noexcept = default;
    disk_storage::disk_storage(fs::path directory, file_hook hook)
        : state_(std::allocate_shared<disk_state>(resource_allocator<disk_state>{})) {
        state_->directory = std::move(directory);
        state_->hook = std::move(hook);
    }
    disk_storage::~disk_storage() = default;
    disk_storage::disk_storage(disk_storage&&) noexcept = default;
    disk_storage& disk_storage::operator=(disk_storage&&) noexcept = default;

    error_code disk_storage::open(error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                const auto result = guarded(
                    [&]() -> error_code {
                        if (!state_ || state_->opened ||
                            state_->directory.native().find('\0') != string::npos)
                            return errc::invalid;
                        auto& state = *state_;
                        fs_request request;
                        const auto status =
                            uv_fs_stat(nullptr, &request.value, state.directory.c_str(), nullptr);
                        if (status < 0) {
                            const auto code = status == UV_ENOENT    ? errc::not_found
                                              : status == UV_EACCES  ? errc::unauthorized
                                              : status == UV_ENOTDIR ? errc::invalid
                                                                     : errc::io_error;
                            if (diagnostic)
                                *diagnostic =
                                    make_error_info(code, "disk.stat", state.directory.native(),
                                                    {-status, std::generic_category()});
                        }
                        if (status == UV_ENOENT)
                            return errc::not_found;
                        if (status == UV_EACCES)
                            return errc::unauthorized;
                        if (status == UV_ENOTDIR)
                            return errc::invalid;
                        if (status < 0)
                            return errc::io_error;
                        if (!(request.value.statbuf.st_mode & S_IFDIR) ||
                            !(request.value.statbuf.st_mode & S_IWUSR))
                            return errc::invalid;
                        vector<string> names;
                        if (auto error = state.filenames(names, diagnostic))
                            return error;
                        std::ranges::sort(names);
                        for (const auto& name : names) {
                            snapshot_file parsed;
                            bool orphan = false;
                            if (parse_snapshot_filename(name, true, parsed))
                                orphan =
                                    !fs::exists(state.directory / name.substr(0, name.size() - 5));
                            else if (parse_snapshot_filename(name, false, parsed))
                                orphan = !fs::exists(state.directory / (name + ".meta"));
                            if (orphan || name.starts_with("tmp-"))
                                (void)state.remove(
                                    name, nullptr); // C cleanup also ignores unlink errors.
                        }
                        persistent_metadata a, b;
                        if (auto error = state.load_slot(1, a, diagnostic))
                            return error;
                        if (auto error = state.load_slot(2, b, diagnostic))
                            return error;
                        if (a.version != 0 && a.version == b.version)
                            return errc::corrupt;
                        state.metadata = a.version > b.version ? a : b;
                        state.opened = true;
                        return {};
                    },
                    diagnostic);
                return result;
            },
            diagnostic, "disk.open", state_ ? &state_->directory : nullptr);
    }
    persistent_metadata disk_storage::metadata() const noexcept {
        return state_ ? state_->metadata : persistent_metadata{};
    }
    error_code disk_storage::probe(disk_capabilities& output, writer_hook hook,
                                   error_info* diagnostic) const noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                if (!state_ || !state_->opened)
                    return errc::invalid;
                return probe_filesystem(state_->directory, output, std::move(hook), diagnostic);
            },
            diagnostic, "disk.probe", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::set_term(raftpp::term value, error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                const auto status = guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        if (state_->metadata_failed)
                            return errc::io_error;
                        auto next = state_->metadata;
                        if (next.version == numeric_limits<uint64_t>::max())
                            return errc::too_big;
                        ++next.version;
                        next.term = value;
                        next.voted_for = 0;
                        return state_->store_metadata(next, diagnostic);
                    },
                    diagnostic);
                return status;
            },
            diagnostic, "disk.set_term", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::set_vote(server_id value, error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                const auto status = guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        if (state_->metadata_failed)
                            return errc::io_error;
                        auto next = state_->metadata;
                        if (next.version == numeric_limits<uint64_t>::max())
                            return errc::too_big;
                        ++next.version;
                        next.voted_for = value;
                        return state_->store_metadata(next, diagnostic);
                    },
                    diagnostic);
                return status;
            },
            diagnostic, "disk.set_vote", state_ ? &state_->directory : nullptr);
    }

    error_code disk_storage::prepare_snapshot(const snapshot& value, uint64_t timestamp,
                                              prepared_snapshot& output,
                                              error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        buffer metadata;
                        if (auto error = encode_snapshot_metadata(value, metadata))
                            return error;
                        auto pending = allocate_owned<prepared_snapshot_state>(
                            resource_allocator<prepared_snapshot_state>{});
                        pending->disk = state_;
                        pending->data_name = "snapshot-" + std::to_string(value.term) + "-" +
                                             std::to_string(value.index) + "-" +
                                             std::to_string(timestamp);
                        pending->metadata_name = pending->data_name + ".meta";
                        pending->metadata_path = state_->path(pending->metadata_name);
                        pending->data_path = state_->path(pending->data_name);
                        array<shared_buffer, 1> metadata_buffers{
                            shared_buffer{std::move(metadata)}};
                        if (auto error = state_->temporary(pending->metadata_name, metadata_buffers,
                                                           pending->metadata, diagnostic))
                            return error;
                        if (auto error = state_->temporary(pending->data_name, value.buffers,
                                                           pending->data, diagnostic))
                            return error;
                        pending->metadata_proc =
                            "/proc/self/fd/" + std::to_string(pending->metadata.get());
                        pending->data_proc = "/proc/self/fd/" + std::to_string(pending->data.get());
                        output.state_ = std::move(pending);
                        return {};
                    },
                    diagnostic);
            },
            diagnostic, "disk.prepare_snapshot", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::commit_snapshot(prepared_snapshot& value,
                                             error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                if (!state_ || !state_->opened || !value.state_ || value.state_->disk != state_)
                    return errc::invalid;
                auto pending = std::move(value.state_);
                bool metadata_published = false, data_published = false;
                const auto status = guarded(
                    [&]() -> error_code {
                        if (auto error = state_->point(file_operation::publish_metadata,
                                                       pending->metadata_name, diagnostic))
                            return error;
                        if (linkat(AT_FDCWD, pending->metadata_proc.c_str(), AT_FDCWD,
                                   pending->metadata_path.c_str(), AT_SYMLINK_FOLLOW) != 0)
                            return state_->native(diagnostic, "disk.snapshot.link_metadata",
                                                  pending->metadata_name, -errno);
                        metadata_published = true;
                        if (auto error = state_->close(pending->metadata, pending->metadata_name,
                                                       diagnostic))
                            return error;
                        if (auto error = state_->point(file_operation::publish_snapshot,
                                                       pending->data_name, diagnostic))
                            return error;
                        if (linkat(AT_FDCWD, pending->data_proc.c_str(), AT_FDCWD,
                                   pending->data_path.c_str(), AT_SYMLINK_FOLLOW) != 0)
                            return state_->native(diagnostic, "disk.snapshot.link_data",
                                                  pending->data_name, -errno);
                        data_published = true;
                        if (auto error =
                                state_->close(pending->data, pending->data_name, diagnostic))
                            return error;
                        return state_->sync_directory(diagnostic);
                    },
                    diagnostic);
                if (status && metadata_published && !data_published)
                    (void)guarded_cleanup(
                        [&] { return state_->remove(pending->metadata_name, nullptr); });
                return status;
            },
            diagnostic, "disk.commit_snapshot", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::list_snapshots(vector<snapshot_file>& output,
                                            error_info* diagnostic) const noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        vector<snapshot_file> result;
                        vector<string> names;
                        if (auto error = state_->filenames(names, diagnostic))
                            return error;
                        for (const auto& name : names) {
                            snapshot_file file;
                            if (!parse_snapshot_filename(name, true, file))
                                continue;
                            const auto data_path =
                                state_->directory / name.substr(0, name.size() - 5);
                            if (!fs::exists(data_path) || fs::file_size(data_path) == 0)
                                continue;
                            result.push_back(std::move(file));
                        }
                        std::ranges::sort(result, snapshot_before);
                        output = std::move(result);
                        return {};
                    },
                    diagnostic);
            },
            diagnostic, "disk.list_snapshots", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::load_snapshot(const snapshot_file& file, snapshot& output,
                                           size_t max_size, error_info* diagnostic) const noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        snapshot_file parsed;
                        if (!parse_snapshot_filename(file.filename, true, parsed) ||
                            parsed.term != file.term || parsed.index != file.index ||
                            parsed.timestamp != file.timestamp)
                            return errc::invalid;
                        buffer metadata;
                        if (auto error = state_->read(file.filename, metadata, 32, 32, diagnostic))
                            return error;
                        if (load_little_endian<uint64_t>(metadata.bytes().first<8>()) !=
                            disk_format_version)
                            return errc::malformed;
                        const auto size =
                            load_little_endian<uint64_t>(metadata.bytes().subspan<24, 8>());
                        if (size == 0 || size > 1024 * 1024)
                            return errc::corrupt;
                        if (auto error = state_->read(file.filename, metadata,
                                                      32 + static_cast<size_t>(size),
                                                      32 + static_cast<size_t>(size), diagnostic))
                            return error;
                        snapshot result;
                        result.term = file.term;
                        result.index = file.index;
                        if (auto error = decode_snapshot_metadata(metadata.bytes(), result))
                            return error;
                        buffer data;
                        if (auto error =
                                state_->read(file.filename.substr(0, file.filename.size() - 5),
                                             data, max_size, 0, diagnostic))
                            return error;
                        if (is_compressed(data.bytes())) {
                            buffer decompressed;
                            if (auto error = decompress(data.bytes(), decompressed, max_size))
                                return error;
                            data = std::move(decompressed);
                        }
                        result.buffers.push_back(shared_buffer{std::move(data)});
                        output = std::move(result);
                        return {};
                    },
                    diagnostic);
            },
            diagnostic, "disk.load_snapshot", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::latest_snapshot(optional<snapshot>& output, size_t max_size,
                                             error_info* diagnostic) const noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        vector<snapshot_file> files;
                        if (auto error = list_snapshots(files, diagnostic))
                            return error;
                        if (files.empty()) {
                            output.reset();
                            return {};
                        }
                        snapshot result;
                        if (auto error = load_snapshot(files.back(), result, max_size, diagnostic))
                            return error;
                        output = std::move(result);
                        return {};
                    },
                    diagnostic);
            },
            diagnostic, "disk.latest_snapshot", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::prune_snapshots(error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        vector<snapshot_file> files;
                        if (auto error = list_snapshots(files, diagnostic))
                            return error;
                        for (size_t i = 0; i + 2 < files.size(); ++i) {
                            if (auto error = state_->remove(files[i].filename, diagnostic))
                                return error;
                            if (auto error = state_->remove(
                                    files[i].filename.substr(0, files[i].filename.size() - 5),
                                    diagnostic))
                                return error;
                        }
                        return state_->sync_directory(diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.prune_snapshots", state_ ? &state_->directory : nullptr);
    }

    error_code disk_storage::list_segments(vector<segment_file>& output,
                                           error_info* diagnostic) const noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        vector<segment_file> result;
                        vector<string> names;
                        if (auto error = state_->filenames(names, diagnostic))
                            return error;
                        for (const auto& name : names) {
                            segment_file file;
                            if (parse_segment_filename(name, file))
                                result.push_back(std::move(file));
                        }
                        std::ranges::sort(result, segment_before);
                        output = std::move(result);
                        return {};
                    },
                    diagnostic);
            },
            diagnostic, "disk.list_segments", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::load_closed(const segment_file& file, vector<entry>& output,
                                         error_info* diagnostic) const noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        segment_file parsed;
                        if (!parse_segment_filename(file.filename, parsed) || parsed.is_open ||
                            file.is_open || parsed.first_index != file.first_index ||
                            parsed.end_index != file.end_index)
                            return errc::invalid;
                        if (file.first_index == 0 || file.end_index < file.first_index)
                            return errc::corrupt;
                        size_t used = 0;
                        return state_->read_segment(file, file.first_index, output, used,
                                                    diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.load_closed", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::write_segment(log_index first, view<const entry> entries,
                                           error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened || first == 0 || entries.empty())
                            return errc::invalid;
                        if (entries.size() > numeric_limits<log_index>::max() - first)
                            return errc::too_big;
                        string name;
                        if (auto error = closed_filename(first, first + entries.size() - 1, name))
                            return error;
                        buffer record;
                        encoding_limits limits{numeric_limits<size_t>::max(),
                                               numeric_limits<size_t>::max(), 262144};
                        if (auto error = encode_record(entries, record, limits))
                            return error;
                        buffer version{8};
                        store_little_endian<uint64_t>(disk_format_version,
                                                      version.bytes().first<8>());
                        array<shared_buffer, 2> data{shared_buffer{std::move(version)},
                                                     shared_buffer{std::move(record)}};
                        return state_->make_file(name, data, diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.write_segment", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::load(io_state& output, bool auto_recovery, size_t max_snapshot_size,
                                  error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        for (unsigned attempt = 0; attempt < 2; ++attempt) {
                            io_state result;
                            result.term = state_->metadata.term;
                            result.voted_for = state_->metadata.voted_for;
                            const auto status = [&]() -> error_code {
                                vector<snapshot_file> snapshots;
                                vector<segment_file> segments;
                                if (auto error = list_snapshots(snapshots, diagnostic))
                                    return error;
                                if (auto error = list_segments(segments, diagnostic))
                                    return error;
                                if (!snapshots.empty()) {
                                    error_code error;
                                    snapshot value;
                                    for (auto it = snapshots.rbegin(); it != snapshots.rend();
                                         ++it) {
                                        error = load_snapshot(*it, value, max_snapshot_size,
                                                              diagnostic);
                                        if (!error)
                                            break;
                                    }
                                    if (error)
                                        return error;
                                    if (value.index == numeric_limits<log_index>::max())
                                        return errc::too_big;
                                    result.start_index = value.index + 1;
                                    result.snapshot = std::move(value);
                                }
                                for (size_t i = 0; i < segments.size(); ++i) {
                                    const auto& file = segments[i];
                                    if (!file.is_open && (file.first_index == 0 ||
                                                          file.end_index < file.first_index))
                                        return errc::corrupt;
                                    if (i > 0 && file.is_open && segments[i - 1].is_open &&
                                        file.counter == segments[i - 1].counter)
                                        return errc::corrupt;
                                }
                                if (result.snapshot && !segments.empty() &&
                                    !segments.front().is_open) {
                                    size_t closed = 0;
                                    while (closed < segments.size() && !segments[closed].is_open)
                                        ++closed;
                                    if (segments[closed - 1].end_index < result.snapshot->index &&
                                        closed == segments.size())
                                        segments.clear();
                                    else {
                                        size_t first = closed - 1;
                                        while (first > 0 && segments[first - 1].end_index ==
                                                                segments[first].first_index - 1)
                                            --first;
                                        if (segments[first].first_index >
                                            result.snapshot->index + 1)
                                            return errc::corrupt;
                                        segments.erase(segments.begin(),
                                                       segments.begin() +
                                                           static_cast<ptrdiff_t>(first));
                                        result.start_index = segments.front().first_index;
                                    }
                                }
                                auto next = result.start_index;
                                for (size_t i = 0; i < segments.size(); ++i) {
                                    const auto& file = segments[i];
                                    if (!file.is_open && file.first_index != next)
                                        return errc::corrupt;
                                    vector<entry> entries;
                                    size_t used = 0;
                                    const auto error =
                                        state_->read_segment(file, next, entries, used, diagnostic);
                                    if (error) {
                                        if (error == errc::corrupt && auto_recovery &&
                                            (file.is_open || i + 1 == segments.size() ||
                                             segments[i + 1].is_open)) {
                                            // Best effort quarantine of the damaged suffix, as in C.
                                            for (size_t j = i; j < segments.size(); ++j) {
                                                (void)guarded_cleanup([&]() -> error_code {
                                                    const auto ticks =
                                                        chrono::duration_cast<chrono::nanoseconds>(
                                                            chrono::system_clock::now()
                                                                .time_since_epoch())
                                                            .count();
                                                    return state_->rename(
                                                        segments[j].filename,
                                                        "corrupt-" + std::to_string(ticks) + "-" +
                                                            segments[j].filename,
                                                        nullptr);
                                                });
                                            }
                                            (void)state_->sync_directory(nullptr);
                                        }
                                        return error;
                                    }
                                    string closed;
                                    if (file.is_open && !entries.empty()) {
                                        if (auto error = closed_filename(
                                                next, next + entries.size() - 1, closed))
                                            return error;
                                    }
                                    const auto count = entries.size();
                                    result.entries.insert(result.entries.end(),
                                                          std::make_move_iterator(entries.begin()),
                                                          std::make_move_iterator(entries.end()));
                                    if (file.is_open) {
                                        if (count == 0) {
                                            if (auto error =
                                                    state_->remove(file.filename, diagnostic))
                                                return error;
                                            if (auto error = state_->sync_directory(diagnostic))
                                                return error;
                                        } else if (auto error = state_->finalize(
                                                       file.filename, closed, used, diagnostic))
                                            return error;
                                    }
                                    next += count;
                                }
                                if (!segments.empty() && result.snapshot &&
                                    next - 1 < result.snapshot->index)
                                    return errc::corrupt;
                                return {};
                            }();
                            if (!status) {
                                output = std::move(result);
                                return {};
                            }
                            if (status != errc::corrupt || !auto_recovery || attempt != 0)
                                return status;
                        }
                        return errc::corrupt;
                    },
                    diagnostic);
            },
            diagnostic, "disk.load", state_ ? &state_->directory : nullptr);
    }
    namespace {
        error_code write_configuration(disk_storage& storage, const configuration& value,
                                       log_index index, term term, size_t block_size,
                                       error_info* diagnostic) {
            buffer encoded;
            if (auto error = value.encode(encoded))
                return error;
            if (block_size < 40 || encoded.size() > block_size - 40)
                return errc::too_big;
            array<entry, 1> entries{
                entry{term, entry_type::change, shared_buffer{std::move(encoded)}}};
            return storage.write_segment(index, entries, diagnostic);
        }
    }
    error_code disk_storage::bootstrap(const configuration& value, size_t block_size,
                                       error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened)
                            return errc::invalid;
                        if (state_->metadata.term != 0)
                            return errc::cannot_bootstrap;
                        if (auto error = set_term(1, diagnostic))
                            return error;
                        if (auto error =
                                write_configuration(*this, value, 1, 1, block_size, diagnostic))
                            return error;
                        return state_->sync_directory(diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.bootstrap", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::recover(const configuration& value, size_t block_size,
                                     bool auto_recovery, error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        io_state loaded;
                        if (auto error = load(loaded, auto_recovery, numeric_limits<size_t>::max(),
                                              diagnostic))
                            return error;
                        const auto last_term = !loaded.entries.empty() ? loaded.entries.back().term
                                               : loaded.snapshot       ? loaded.snapshot->term
                                                                       : 0;
                        if (last_term == 0)
                            return errc::invalid;
                        if (auto error = write_configuration(
                                *this, value, loaded.start_index + loaded.entries.size(), last_term,
                                block_size, diagnostic))
                            return error;
                        return state_->sync_directory(diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.recover", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::truncate(log_index index, error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (index == 0)
                            return errc::invalid;
                        vector<segment_file> files;
                        if (auto error = list_segments(files, diagnostic))
                            return error;
                        const auto found = std::ranges::find_if(files, [&](const auto& file) {
                            return !file.is_open && file.first_index <= index &&
                                   index <= file.end_index;
                        });
                        if (found == files.end())
                            return errc::not_found;
                        if (index > found->first_index) {
                            vector<entry> entries;
                            if (auto error = load_closed(*found, entries, diagnostic))
                                return error;
                            if (auto error = write_segment(
                                    found->first_index,
                                    view<const entry>{entries}.first(
                                        static_cast<size_t>(index - found->first_index)),
                                    diagnostic))
                                return error;
                        }
                        for (auto it = found; it != files.end() && !it->is_open; ++it) {
                            if (auto error = state_->remove(it->filename, diagnostic))
                                return error;
                        }
                        return state_->sync_directory(diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.truncate", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::prune_segments(log_index last_index, size_t trailing,
                                            error_info* diagnostic, bool local) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (last_index == 0)
                            return errc::invalid;
                        vector<segment_file> files;
                        if (auto error = list_segments(files, diagnostic))
                            return error;
                        if (last_index <= trailing)
                            return {};
                        for (const auto& file : files) {
                            if (file.is_open || (local && file.end_index > last_index) ||
                                (trailing != 0 && file.end_index >= last_index - trailing + 1))
                                break;
                            if (auto error = state_->remove(file.filename, diagnostic))
                                return error;
                        }
                        return state_->sync_directory(diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.prune_segments", state_ ? &state_->directory : nullptr);
    }

    error_code disk_storage::prepare_segment(uint64_t counter, size_t size, file_handle& output,
                                             error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded([&]() -> error_code {
                    if (!state_ || !state_->opened || counter == 0 || size == 0)
                        return errc::invalid;
                    if (size > static_cast<uint64_t>(numeric_limits<int64_t>::max()))
                        return errc::too_big;
                    const auto name = "open-" + std::to_string(counter), path = state_->path(name);
                    file_handle file;
                    if (auto error = state_->open_file(path, name, O_WRONLY | O_CREAT | O_EXCL,
                                                       0600, file, diagnostic))
                        return error;
                    const auto status = guarded(
                        [&]() -> error_code {
                            if (auto error = state_->allocate(file, name, size, diagnostic))
                                return error;
                            return state_->sync_directory(diagnostic);
                        },
                        diagnostic);
                    if (status) {
                        (void)file.close();
                        fs_request request;
                        (void)uv_fs_unlink(nullptr, &request.value, path.c_str(), nullptr);
                        return status;
                    }
                    output = std::move(file);
                    return {};
                });
            },
            diagnostic, "disk.prepare_segment", state_ ? &state_->directory : nullptr);
    }
    error_code disk_storage::finalize_segment(uint64_t counter, size_t used, log_index first,
                                              log_index last, error_info* diagnostic) noexcept {
        return capture_diagnostic(
            [&](error_info* diagnostic) -> error_code {
                return guarded(
                    [&]() -> error_code {
                        if (!state_ || !state_->opened || counter == 0)
                            return errc::invalid;
                        const auto name = "open-" + std::to_string(counter);
                        if (used == 0) {
                            if (auto error = state_->remove(name, diagnostic))
                                return error;
                            return state_->sync_directory(diagnostic);
                        }
                        string closed;
                        if (auto error = closed_filename(first, last, closed))
                            return error;
                        return state_->finalize(name, closed, used, diagnostic);
                    },
                    diagnostic);
            },
            diagnostic, "disk.finalize_segment", state_ ? &state_->directory : nullptr);
    }

}
