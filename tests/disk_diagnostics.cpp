#include <raftpp.hpp>
#include "disk.hpp"
#include "file_backend.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
#include <uv.h>
#include <unistd.h>

namespace r = raftpp;
namespace d = raftpp::detail;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace {
    void check(bool value, r::string_view text) {
        if (!value)
            throw std::runtime_error(r::string{text});
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-disk-error-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "temporary directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    struct loop {
        uv_loop_t value{};
        loop() {
            check(!uv_loop_init(&value), "loop initialization");
        }
        ~loop() {
            uv_run(&value, UV_RUN_DEFAULT);
            if (uv_loop_close(&value))
                std::abort();
        }
        void run() {
            uv_run(&value, UV_RUN_DEFAULT);
        }
    };
    r::configuration members(unsigned count = 1) {
        r::configuration result;
        for (unsigned i = 1; i <= count; ++i)
            check(!result.add(i, "node-" + std::to_string(i), r::role::voter), "configuration");
        return result;
    }
    r::snapshot snapshot(r::log_index index) {
        r::snapshot value;
        value.term = 2;
        value.index = index;
        value.configuration = members();
        value.configuration_index = 1;
        value.buffers.emplace_back(r::buffer{8});
        return value;
    }
    void publish(d::disk_storage& disk, r::log_index index) {
        d::prepared_snapshot prepared;
        check(!disk.prepare_snapshot(snapshot(index), index, prepared) &&
                  !disk.commit_snapshot(prepared),
              "snapshot fixture");
    }
    r::vector<r::entry> entries() {
        return {{2, r::entry_type::command, r::shared_buffer{r::buffer{8}}}};
    }

    void metadata() {
        directory dir;
        d::disk_storage disk{dir.path};
        r::error_info info;
        check(!disk.open(&info), "metadata store open");
        r::fs::create_directory(dir.path / "metadata1");
        check(disk.set_term(1, &info) == r::errc::io_error &&
                  info.cause == std::errc::is_a_directory &&
                  info.text().find((dir.path / "metadata1").native()) != r::string_view::npos,
              "actual metadata open errno/path");
        r::fs::remove(dir.path / "metadata1");
        check(!disk.set_term(1, &info) && !info.code && info.text().empty(),
              "successful metadata retry clears supplied output");
        for (unsigned mode = 0; mode < 3; ++mode) {
            directory failed;
            d::disk_storage storage{
                failed.path, [&](auto operation, auto name) -> r::error_code {
                    if (mode == 0 && operation == d::file_operation::write)
                        return r::errc::no_space;
                    if (mode == 1 && operation == d::file_operation::sync_file)
                        throw r::fs::filesystem_error(
                            "injected fsync failure", failed.path / name,
                            std::make_error_code(std::errc::read_only_file_system));
                    if (mode == 2 && operation == d::file_operation::close)
                        return r::errc::unauthorized;
                    return {};
                }};
            check(!storage.open(), "injected metadata store open");
            const auto status = storage.set_term(1, &info);
            check(info.code == status && info.text().find("metadata1") != r::string_view::npos,
                  "injected metadata operation owns path");
            check(mode == 0   ? status == r::errc::no_space
                  : mode == 1 ? status == r::errc::io_error
                              : status == r::errc::unauthorized,
                  "metadata errors preserve original status mappings");
            if (mode == 1)
                check(info.cause == std::errc::read_only_file_system,
                      "filesystem exception retains native cause");
            check(storage.set_vote(2, &info) == r::errc::io_error && !info.cause,
                  "locked metadata guard does not reuse previous native cause");
        }
    }

    void snapshots() {
        directory dir;
        d::disk_storage disk{dir.path};
        check(!disk.open() && !disk.set_term(2), "snapshot diagnostic store");
        publish(disk, 5);
        publish(disk, 10);
        {
            std::fstream file{dir.path / "snapshot-2-10-10.meta",
                              std::ios::in | std::ios::out | std::ios::binary};
            const char byte = 99;
            file.write(&byte, 1);
        }
        r::io_state recovered;
        auto info = r::make_error_info(r::errc::io_error, "old failure");
        check(!disk.load(recovered, true, r::numeric_limits<r::size_t>::max(), &info) &&
                  recovered.snapshot && recovered.snapshot->index == 5 && !info.code && !info.cause,
              "successful snapshot fallback clears ignored failure diagnosis");
        d::prepared_snapshot prepared;
        check(!disk.prepare_snapshot(snapshot(20), 20, prepared),
              "prepare conflicting publication");
        {
            std::ofstream conflict{dir.path / "snapshot-2-20-20"};
            conflict << "existing";
        }
        check(disk.commit_snapshot(prepared, &info) == r::errc::io_error &&
                  info.cause == std::errc::file_exists &&
                  info.text().find("snapshot-2-20-20") != r::string_view::npos && prepared.empty(),
              "linkat errno survives failed publication cleanup");

        std::atomic<bool> valid{true};
        r::array<std::thread, 4> threads;
        for (unsigned i = 0; i < threads.size(); ++i)
            threads[i] = std::thread{[&, i] {
                d::snapshot_file missing{2, i + 30, 30,
                                         "snapshot-2-" + std::to_string(i + 30) + "-30.meta"};
                for (unsigned n = 0; n < 16; ++n) {
                    r::snapshot output;
                    r::error_info result;
                    if (disk.load_snapshot(missing, output, 4096, &result) != r::errc::io_error ||
                        result.cause != std::errc::no_such_file_or_directory ||
                        result.text().find(missing.filename) == r::string_view::npos)
                        valid = false;
                }
            }};
        for (auto& thread : threads)
            thread.join();
        check(valid, "concurrent disk calls keep independent owning paths/causes");

        directory worker_dir;
        loop event_loop;
        std::atomic<bool> enabled{false}, removed{false};
        auto worker_disk = std::make_shared<d::disk_storage>(
            worker_dir.path, [&](auto operation, auto name) -> r::error_code {
                if (enabled && operation == d::file_operation::read && name.ends_with(".meta") &&
                    !removed.exchange(true))
                    r::fs::remove(worker_dir.path / "snapshot-2-10-10");
                return {};
            });
        check(!worker_disk->open(), "worker snapshot diagnostic store");
        publish(*worker_disk, 10);
        d::file_backend files{event_loop.value, worker_disk};
        check(!files.open(11), "worker coordinator open");
        enabled = true;
        bool done = false;
        r::error_info saved;
        check(!files.snapshot_get([&](auto status, auto) {
            saved = files.error_details();
            done = status == r::errc::io_error;
            (void)files.close();
        }),
              "snapshot worker get accepted");
        event_loop.run();
        check(done && saved.cause == std::errc::no_such_file_or_directory &&
                  saved.text().find("snapshot-2-10-10") != r::string_view::npos,
              "real worker filesystem errno/path reaches coordinator completion");
    }

    void segments() {
        directory dir;
        d::disk_storage disk{dir.path};
        check(!disk.open(), "segment diagnostic store");
        r::error_info info;
        d::file_handle first;
        check(!disk.prepare_segment(1, 4096, first), "first prepared segment");
        const auto fd = first.get();
        check(disk.prepare_segment(1, 4096, first, &info) == r::errc::io_error &&
                  info.cause == std::errc::file_exists && first.get() == fd,
              "prepare O_EXCL cause preserves output handle");
        check(!first.close(), "prepared segment close");
        r::fs::remove(dir.path / "open-1");
        check(disk.finalize_segment(1, 8, 1, 1, &info) == r::errc::io_error &&
                  info.cause == std::errc::no_such_file_or_directory,
              "finalize captures missing active segment");
        directory failed;
        d::disk_storage storage{failed.path, [](auto operation, auto) -> r::error_code {
                                    if (operation == d::file_operation::write)
                                        return r::errc::no_space;
                                    if (operation == d::file_operation::remove)
                                        return r::errc::unauthorized;
                                    return {};
                                }};
        check(!storage.open(), "segment cleanup diagnostic store");
        const auto input = entries();
        check(storage.write_segment(1, input, &info) == r::errc::no_space &&
                  info.code == r::errc::no_space && info.text().starts_with("write:") &&
                  info.text().find("tmp-") != r::string_view::npos,
              "secondary cleanup failure preserves primary write diagnosis");
        allocation_failure::after(0);
        const auto status = disk.write_segment(1, input, &info);
        allocation_failure::after(-1);
        check(status == r::errc::out_of_memory && info.code == status && !info.text().empty(),
              "OOM diagnosis survives allocation failure");
        d::disk_capabilities capabilities;
        check(disk.probe(
                  capabilities,
                  [](auto point, auto value) {
                      return point == d::writer_operation::setup ? r::int64_t{-EACCES} : value;
                  },
                  &info) == r::errc::io_error &&
                  info.cause == std::errc::permission_denied,
              "capability probe preserves syscall cause");
    }

    struct network : r::transport {
        r::function<void(r::message)> receive;
        r::error_code init(r::server_id, r::string_view) override {
            return {};
        }
        r::error_code start(r::function<void(r::message)> callback) override {
            receive = std::move(callback);
            return {};
        }
        r::error_code send(r::shared<const r::message>, r::completion) override {
            return r::errc::no_connection;
        }
        void close(r::close_callback callback) noexcept override {
            receive = {};
            if (callback)
                callback();
        }
    };
    struct machine : r::state_machine {
        r::error_code apply(r::view<const r::byte>, r::shared<void>&) override {
            return {};
        }
        r::error_code restore(r::shared_buffer) override {
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>&) override {
            return {};
        }
    };
    void node() {
        directory dir;
        loop event_loop;
        auto net = std::make_shared<network>();
        auto backend = std::make_shared<r::uv_io>(event_loop.value, dir.path, net);
        r::node instance{1, "node-1", backend, std::make_shared<machine>()};
        check(!instance.bootstrap(members(3)) && !instance.start(), "native node start");
        r::fs::create_directory(dir.path / "metadata2");
        r::request_vote vote;
        vote.term = 2;
        vote.candidate_id = 2;
        vote.last_log_index = 1;
        vote.last_log_term = 1;
        net->receive({2, "node-2", vote});
        const auto status = instance.error();
        const auto info = instance.error_details();
        check(!instance.close(), "native node close");
        event_loop.run();
        check(status == r::errc::io_error && info.cause == std::errc::is_a_directory &&
                  info.text().find((dir.path / "metadata2").native()) != r::string_view::npos,
              "actual metadata syscall failure reaches UV and node before cleanup");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected disk diagnostic suite");
        const r::string_view suite{argv[1]};
        if (suite == "disk_metadata_diagnostics")
            metadata();
        else if (suite == "disk_snapshot_diagnostics")
            snapshots();
        else if (suite == "disk_segment_diagnostics")
            segments();
        else if (suite == "disk_node_diagnostics")
            node();
        else
            throw std::runtime_error("unknown disk diagnostic suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
