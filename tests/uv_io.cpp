#include <raftpp.hpp>
#include "file_backend.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#include <uv.h>
#include <unistd.h>

namespace r = raftpp;
namespace d = raftpp::detail;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace {
    std::exception_ptr failure;
    void check(bool value, r::string_view text,
               std::source_location where = std::source_location::current()) {
        if (!value) {
            auto error = std::make_exception_ptr(
                std::runtime_error(r::string{where.file_name()} + ":" +
                                   std::to_string(where.line()) + ": " + r::string{text}));
            if (!failure)
                failure = error;
            std::rethrow_exception(error);
        }
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string pattern = "/tmp/raftpp-uv-XXXXXX";
            check(mkdtemp(pattern.data()) != nullptr, "mkdtemp");
            path = pattern;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    struct loop {
        uv_loop_t value{};
        loop() {
            check(uv_loop_init(&value) == 0, "loop init");
        }
        ~loop() {
            if (uv_loop_close(&value) != 0) {
                if (failure)
                    try {
                        std::rethrow_exception(failure);
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << '\n';
                    }
                std::cerr << "live handles on loop destruction\n";
                std::abort();
            }
        }
        template <typename F> void until(F&& condition) {
            auto deadline = uv_hrtime() + 15000000000ULL;
            while (!condition() && uv_hrtime() < deadline) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                if (failure)
                    std::rethrow_exception(failure);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            check(condition(), "operation completed before timeout");
        }
        void run() {
            (void)uv_run(&value, UV_RUN_DEFAULT);
            if (failure)
                std::rethrow_exception(failure);
        }
    };
    r::configuration configuration() {
        r::configuration value;
        check(!value.add(1, "one", r::role::voter), "configuration");
        return value;
    }
    r::shared<d::disk_storage> disk(const directory& dir, d::file_hook hook = {}) {
        auto result = std::make_shared<d::disk_storage>(dir.path, std::move(hook));
        check(!result->open(), "storage open");
        return result;
    }
    d::file_backend_options settings() {
        d::file_backend_options result;
        result.log.segment_size = 4096;
        result.log.disk_retry = 1;
        result.log.capabilities = d::disk_capabilities{512, false, false};
        return result;
    }
    r::shared<const r::entry_batch> batch(unsigned count = 8) {
        auto result = std::make_shared<r::entry_batch>();
        for (unsigned i = 0; i < count; ++i) {
            r::buffer value{8};
            std::ranges::fill(value.bytes(), static_cast<r::byte>(i));
            result->push_back(std::make_shared<r::entry>(
                r::entry{2, r::entry_type::command, r::shared_buffer{std::move(value)}}));
        }
        return result;
    }
    r::shared<const r::snapshot> snapshot(r::log_index index) {
        auto result = std::make_shared<r::snapshot>();
        result->index = index;
        result->term = 2;
        result->configuration = configuration();
        result->configuration_index = 1;
        for (unsigned i = 0; i < 2; ++i) {
            r::buffer value{8};
            std::ranges::fill(value.bytes(), static_cast<r::byte>(index + i));
            result->buffers.push_back(r::shared_buffer{std::move(value)});
        }
        return result;
    }
    r::size_t descriptors() {
        return static_cast<r::size_t>(
            std::distance(r::fs::directory_iterator{"/proc/self/fd"}, r::fs::directory_iterator{}));
    }
    void append(loop& event_loop, d::file_backend& files, r::log_index index, unsigned count = 8) {
        bool done = false;
        check(!files.append(index, batch(count),
                            [&](auto status) {
                                check(!status, "append completion");
                                done = true;
                            }),
              "append accepted");
        event_loop.until([&] { return done; });
    }
    void put(loop& event_loop, d::file_backend& files, unsigned trailing, r::log_index index) {
        bool done = false;
        check(!files.snapshot_put(trailing, snapshot(index),
                                  [&](auto status) {
                                      check(!status, "put completion");
                                      done = true;
                                  }),
              "put accepted");
        event_loop.until([&] { return done; });
    }
    void close(loop& event_loop, d::file_backend& files) {
        bool done = false;
        check(!files.close([&] { done = true; }), "close accepted");
        event_loop.run();
        check(done, "closed after native resources");
    }

    void snapshots() {
        for (unsigned trailing : {0U, 3U}) {
            directory cpp;
            loop event_loop;
            auto storage = disk(cpp);
            d::file_backend files{event_loop.value, storage, settings()};
            check(!files.open(1), "file backend open");
            r::vector<r::shared<const r::snapshot>> values;
            r::vector<r::shared<const r::entry_batch>> entries;
            for (unsigned i = 0; i < 3; ++i) {
                auto index = trailing ? 8 * (i + 1) : 50 * (i + 1);
                entries.push_back(batch());
                values.push_back(snapshot(index));
                append(event_loop, files, trailing ? 8 * i + 1 : i ? 50 * i + 1 : 1);
                put(event_loop, files, trailing, index);
            }
            unsigned gets = 0;
            for (unsigned i = 0; i < 4; ++i)
                check(!files.snapshot_get([&](auto status, auto value) {
                    check(!status && value && value->index == values.back()->index &&
                              value->buffers.size() == 1 && value->buffers[0].size() == 16,
                          "latest snapshot get");
                    ++gets;
                }),
                      "parallel get accepted");
            event_loop.until([&] { return gets == 4; });
            close(event_loop, files);
            r::vector<d::snapshot_file> remaining;
            check(!storage->list_snapshots(remaining) && remaining.size() == 2,
                  "keep latest two snapshots");
            r::io_state loaded;
            check(!storage->load(loaded) && loaded.snapshot &&
                      loaded.snapshot->index == values.back()->index,
                  "recover snapshot after coordinator close");
        }
    }
    // Combined barrier contract from cowsql/raft test_uv_truncate_snapshot.c;
    // see LICENSE.md. Both backends receive the same queued operations.
    void snapshot_truncate() {
        unsigned cases = 0;
        for (const unsigned size : {8U, 512U, 4096U}) {
            directory cpp;
            loop event_loop;
            auto storage = disk(cpp);
            d::file_backend files{event_loop.value, storage, settings()};
            check(!files.open(1), "combined snapshot/truncate open");
            auto all = std::make_shared<r::entry_batch>();
            for (unsigned i = 0; i < 25; ++i) {
                r::buffer payload{size};
                std::ranges::fill(payload.bytes(), static_cast<r::byte>(i));
                all->push_back(std::make_shared<r::entry>(
                    r::entry{2, r::entry_type::command, r::shared_buffer{std::move(payload)}}));
            }
            unsigned old_done = 0, put_done = 0, replacement_done = 0;
            for (unsigned i = 0; i < 3; ++i) {
                auto group = std::make_shared<r::entry_batch>(all->begin() + i * 8,
                                                              all->begin() + (i + 1) * 8);
                check(!files.append(i * 8 + 1, std::move(group),
                                    [&](auto status) {
                                        check(!status, "pre-snapshot append completes");
                                        ++old_done;
                                    }),
                      "queue pre-snapshot batch");
            }
            auto value = snapshot(6);
            check(!files.snapshot_put(8192, value,
                                      [&](auto status) {
                                          check(!status, "combined snapshot completion");
                                          ++put_done;
                                      }),
                  "queue local snapshot");
            check(!files.truncate(8), "queue truncate after snapshot");
            auto replacement = std::make_shared<r::entry_batch>(1, all->back());
            check(!files.append(
                      8, std::move(replacement),
                      [&](auto status) {
                          check(!status && old_done == 3,
                                "replacement follows accepted prefix writes and truncation");
                          ++replacement_done;
                      }),
                  "queue replacement after truncation");
            event_loop.until(
                [&] { return old_done == 3 && put_done == 1 && replacement_done == 1; });
            close(event_loop, files);
            r::io_state loaded;
            check(!storage->load(loaded) && loaded.snapshot && loaded.snapshot->index == 6 &&
                      loaded.start_index == 1 && loaded.entries.size() == 8 &&
                      loaded.entries.back().payload.bytes()[0] == r::byte{24},
                  "recovered prefix and replacement respect snapshot/truncate boundary");
            ++cases;
        }
        std::cout << "snapshot/truncate contracts: " << cases << " actual queued-operation cases\n";
    }

    void ordering() {
        for (unsigned trailing : {0U, 3U}) {
            directory dir;
            loop event_loop;
            std::atomic<bool> entered = false, release = false;
            auto storage = disk(dir, [&](auto operation, auto name) -> r::error_code {
                if (operation == d::file_operation::publish_snapshot &&
                    name.starts_with("snapshot-")) {
                    entered = true;
                    while (!release.load())
                        std::this_thread::sleep_for(r::chrono::milliseconds{1});
                }
                return {};
            });
            d::file_backend files{event_loop.value, storage, settings()};
            check(!files.open(1), "open");
            append(event_loop, files, 1);
            bool published = false, appended = false;
            check(!files.snapshot_put(trailing, snapshot(trailing ? 8 : 50),
                                      [&](auto status) {
                                          check(!status, "publication completion");
                                          published = true;
                                      }),
                  "snapshot accepted");
            event_loop.until([&] { return entered.load(); });
            check(!files.append(trailing ? 9 : 51, batch(),
                                [&](auto status) {
                                    check(!status, "following append");
                                    appended = true;
                                }),
                  "following append accepted");
            if (trailing)
                event_loop.until([&] { return appended; });
            else
                for (unsigned i = 0; i < 20; ++i) {
                    (void)uv_run(&event_loop.value, UV_RUN_NOWAIT);
                    std::this_thread::sleep_for(r::chrono::milliseconds{1});
                }
            check(!published && (trailing ? appended : !appended),
                  "local snapshot allows writes; install blocks writes until publication/prune");
            release = true;
            event_loop.until([&] { return published && appended; });
            close(event_loop, files);
            r::io_state state;
            check(!storage->load(state) && state.snapshot && state.entries.size() >= 8,
                  "post-snapshot suffix survives cleanup");
        }
    }
    void failures() {
        for (unsigned mode = 0; mode < 5; ++mode) {
            directory dir;
            loop event_loop;
            std::atomic<unsigned> attempts = 0;
            auto storage = disk(dir, [&](auto operation, auto name) -> r::error_code {
                if (operation == d::file_operation::allocate && name.starts_with("snapshot-") &&
                    mode == 0 && attempts.fetch_add(1) < 3)
                    return r::errc::no_space;
                if (operation == d::file_operation::publish_snapshot && mode == 1)
                    return r::errc::io_error;
                return {};
            });
            auto options = settings();
            options.hook = [mode](auto operation) -> r::error_code {
                if ((mode == 2 && operation == d::file_backend_operation::queue_prepare) ||
                    (mode == 3 && operation == d::file_backend_operation::queue_publish) ||
                    (mode == 4 && operation == d::file_backend_operation::queue_get))
                    return r::errc::io_error;
                return {};
            };
            d::file_backend files{event_loop.value, storage, options};
            check(!files.open(1), "open");
            unsigned calls = 0;
            auto status = files.snapshot_put(0, snapshot(50), [&](auto status) {
                check(mode == 0 || mode == 4 ? !status : status == r::errc::io_error,
                      "completion status");
                ++calls;
            });
            if (mode == 2)
                check(status == r::errc::io_error && calls == 0,
                      "prepare queue immediate failure has no callback");
            else {
                check(!status, "put accepted");
                event_loop.until([&] { return calls == 1; });
            }
            if (mode == 0)
                check(attempts >= 5, "preparation retried after no-space failures");
            if (mode == 4)
                check(files.snapshot_get([&](auto, auto) { ++calls; }) == r::errc::io_error &&
                          calls == 1,
                      "get queue immediate failure has no callback");
            else
                append(event_loop, files, mode == 2 ? 1 : 51);
            close(event_loop, files);
            check(!files.last_error(), "request failure does not strand barrier or corrupt writer");
        }
    }
    void closing() {
        for (unsigned mode = 0; mode < 3; ++mode) {
            directory dir;
            loop event_loop;
            std::atomic<bool> entered = false, release = false;
            auto storage = disk(dir, [&](auto operation, auto name) -> r::error_code {
                const bool match =
                    name.starts_with("snapshot-") &&
                    ((mode == 0 && operation == d::file_operation::allocate) ||
                     (mode == 1 && operation == d::file_operation::publish_snapshot) ||
                     (mode == 2 && operation == d::file_operation::read));
                if (match) {
                    entered = true;
                    while (!release.load())
                        std::this_thread::sleep_for(r::chrono::milliseconds{1});
                }
                return {};
            });
            auto files = std::make_unique<d::file_backend>(event_loop.value, storage, settings());
            check(!files->open(1), "open");
            if (mode == 2)
                put(event_loop, *files, 0, 50);
            bool done = false, closed = false;
            auto value = snapshot(50);
            r::weak<const r::snapshot> observer = value;
            if (mode == 2)
                check(!files->snapshot_get([&](auto status, auto result) {
                    check(!status && result && !closed,
                          "active get completes normally before close");
                    done = true;
                }),
                      "get accepted");
            else
                check(!files->snapshot_put(
                          0, value,
                          [&](auto status) {
                              check(mode == 0 ? status == r::errc::canceled : !status,
                                    "prepare canceled; publication drains normally");
                              check(!closed && !observer.expired(),
                                    "accepted snapshot lives through completion");
                              done = true;
                          }),
                      "put accepted");
            value.reset();
            event_loop.until([&] { return entered.load(); });
            check(!files->close([&] {
                check(done && observer.expired(), "payload released before close");
                closed = true;
            }),
                  "close accepted");
            files.reset();
            check(!closed, "close waits for native worker after owner destruction");
            release = true;
            event_loop.run();
            check(done && closed, "close drains snapshots and handles");
        }
        directory dir;
        loop event_loop;
        std::atomic<unsigned> attempts = 0;
        auto storage = disk(dir, [&](auto operation, auto name) -> r::error_code {
            if (operation == d::file_operation::allocate && name.starts_with("snapshot-")) {
                ++attempts;
                return r::errc::no_space;
            }
            return {};
        });
        auto options = settings();
        options.log.disk_retry = 60000;
        d::file_backend files{event_loop.value, storage, options};
        check(!files.open(1), "open retry backend");
        bool canceled = false;
        check(!files.snapshot_put(0, snapshot(50),
                                  [&](auto status) {
                                      check(status == r::errc::canceled, "retry canceled");
                                      canceled = true;
                                  }),
              "retry put");
        event_loop.until([&] { return attempts.load() != 0; });
        close(event_loop, files);
        check(canceled, "close does not wait for disk retry deadline");
    }
    void allocations() {
        for (unsigned kind = 0; kind < 3; ++kind) {
            bool failed = false, succeeded = false;
            for (std::ptrdiff_t position = 0; position < 45; ++position) {
                directory dir;
                loop event_loop;
                const auto before = descriptors();
                {
                    auto storage = disk(dir);
                    d::file_backend files{event_loop.value, storage, settings()};
                    if (kind)
                        check(!files.open(1), "open before operation sweep");
                    auto value = snapshot(50);
                    unsigned calls = 0;
                    r::error_code status;
                    r::completion completed = [&](auto error) {
                        check(!error, "accepted operation completes");
                        ++calls;
                    };
                    r::snapshot_callback got = [&](auto error, auto) {
                        check(!error, "get completes");
                        ++calls;
                    };
                    allocation_failure::after(position);
                    if (kind == 0)
                        status = files.open(1);
                    else if (kind == 1)
                        status = files.snapshot_put(0, value, completed);
                    else
                        status = files.snapshot_get(got);
                    allocation_failure::after(-1);
                    if (status) {
                        check(status == r::errc::out_of_memory, "allocation error mapping");
                        failed = true;
                        check(calls == 0, "immediate allocation failure has no callback");
                    } else {
                        succeeded = true;
                        if (kind)
                            event_loop.until([&] { return calls == 1; });
                    }
                    bool done = false;
                    r::close_callback callback = [&] { done = true; };
                    allocation_failure::after(0);
                    const auto close_status = files.close(std::move(callback));
                    allocation_failure::after(-1);
                    check(!close_status, "allocation-free close");
                    event_loop.run();
                    check(done, "close after failure");
                }
                check(descriptors() == before, "allocation sweep descriptor cleanup");
            }
            check(failed && succeeded, "allocation sweep covers failures and successful calls");
        }
    }
    void diagnostics() {
        directory dir;
        loop event_loop;
        std::atomic<bool> enabled{false};
        std::atomic<unsigned> reads{0};
        auto storage = disk(dir, [&](auto operation, auto) -> r::error_code {
            if (enabled && operation == d::file_operation::read)
                return reads.fetch_add(1) % 2 ? r::errc::unauthorized : r::errc::io_error;
            return {};
        });
        d::file_backend files{event_loop.value, storage, settings()};
        check(!files.open(1), "snapshot diagnostic open");
        put(event_loop, files, 0, 10);
        enabled = true;
        unsigned completed = 0, io = 0, unauthorized = 0;
        r::array<r::error_info, 4> captured{};
        for (unsigned i = 0; i < 4; ++i)
            check(!files.snapshot_get([&, i](auto status, auto value) {
                const auto info = files.error_details();
                check(!value && info.code == status && info.text().starts_with("snapshot.get"),
                      "snapshot worker diagnostic belongs to current completion");
                if (status == r::errc::io_error)
                    ++io;
                else if (status == r::errc::unauthorized)
                    ++unauthorized;
                captured[i] = info;
                ++completed;
            }),
                  "concurrent snapshot diagnostic get");
        event_loop.until([&] { return completed == 4; });
        close(event_loop, files);
        check(io == 2 && unauthorized == 2 && captured[0].code,
              "concurrent snapshot failures remain distinct after close");

        directory failed;
        std::atomic<bool> fail_reads{false};
        auto other_disk = disk(failed, [&](auto operation, auto) -> r::error_code {
            return fail_reads && operation == d::file_operation::read
                       ? r::make_error_code(r::errc::unauthorized)
                       : r::error_code{};
        });
        auto options = settings();
        options.log.writer = [](auto operation, auto value) {
            return operation == d::writer_operation::poll_init ? r::int64_t{-EACCES} : value;
        };
        d::file_backend broken{event_loop.value, other_disk, options};
        check(!broken.open(1), "partial writer diagnostic backend open");
        put(event_loop, broken, 0, 10);
        unsigned callbacks = 0;
        r::error_info saved;
        check(!broken.append(
                  11, batch(1),
                  [&](auto status) {
                      saved = broken.error_details();
                      check(status == r::errc::io_error &&
                                saved.cause == std::errc::permission_denied &&
                                saved.text().starts_with("log.writer.open: open-1: uv_poll_init"),
                            "partial writer initialization diagnosis reaches file coordinator");
                      ++callbacks;
                  }),
              "partial writer initialization is an accepted append");
        fail_reads = true;
        bool retrieved = false;
        check(!broken.snapshot_get([&](auto status, auto value) {
            check(status == r::errc::unauthorized && !value &&
                      broken.error_details().code == status &&
                      broken.error_details().text().starts_with("snapshot.get"),
                  "current snapshot completion overrides previous fatal writer diagnosis");
            retrieved = true;
        }),
              "read accepted after writer failure");
        event_loop.until([&] { return retrieved; });
        bool closed = false;
        check(!broken.close([&] { closed = true; }), "partial writer failure close");
        event_loop.run();
        check(callbacks == 1 && closed && saved.cause == std::errc::permission_denied,
              "failed writer retains context through cleanup");
        directory retried;
        std::atomic<unsigned> writes{0};
        auto retry_disk = disk(retried, [&](auto operation, auto) -> r::error_code {
            return operation == d::file_operation::write && writes.fetch_add(1) == 0
                       ? r::make_error_code(r::errc::io_error)
                       : r::error_code{};
        });
        d::file_backend successful{event_loop.value, retry_disk, settings()};
        check(!successful.open(1), "snapshot retry diagnostic open");
        put(event_loop, successful, 0, 10);
        check(successful.error_details().code == r::errc::io_error &&
                  successful.error_details().text().starts_with("snapshot.prepare:") &&
                  successful.error_details().text().find("snapshot-2-10-") != r::string_view::npos,
              "successful snapshot retry retains preparation diagnostic");
        close(event_loop, successful);

        directory canceled_dir;
        std::atomic<bool> entered{false}, released{false};
        auto cancel_disk = disk(canceled_dir, [&](auto operation, auto name) -> r::error_code {
            if (operation == d::file_operation::write && name.starts_with("snapshot-")) {
                entered = true;
                while (!released.load())
                    std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            return {};
        });
        d::file_backend canceling{event_loop.value, cancel_disk, settings()};
        check(!canceling.open(1), "cancel diagnostic backend open");
        unsigned canceled = 0;
        check(!canceling.snapshot_put(
                  0, snapshot(10),
                  [&](auto status) {
                      check(status == r::errc::canceled && canceling.error_details().code == status,
                            "canceled snapshot completion publishes its own diagnostic");
                      ++canceled;
                  }),
              "cancel diagnostic snapshot accepted");
        event_loop.until([&] { return entered.load(); });
        check(!canceling.close(), "cancel diagnostic close");
        released = true;
        event_loop.run();
        check(canceled == 1 && canceling.closed(),
              "cancellation diagnostic follows worker completion and cleanup");
    }

    struct network : r::transport {
        r::error_code init_error, start_error, send_error;
        r::function<void(r::message)> receive;
        r::list<r::pair<r::shared<const r::message>, r::completion>> pending;
        unsigned closes = 0;
        unsigned connect_retry = 1000;
        void set_connect_retry_delay(unsigned value) noexcept override {
            connect_retry = value;
        }
        r::error_code init(r::server_id, r::string_view) override {
            return init_error;
        }
        r::error_code start(r::function<void(r::message)> callback) override {
            receive = std::move(callback);
            return start_error;
        }
        r::error_code send(r::shared<const r::message> value, r::completion callback) override {
            if (send_error)
                return send_error;
            pending.emplace_back(std::move(value), std::move(callback));
            return {};
        }
        void close(r::close_callback callback) noexcept override {
            ++closes;
            receive = {};
            while (!pending.empty()) {
                auto request = std::move(pending.front());
                pending.pop_front();
                request.second(r::errc::canceled);
            }
            callback();
        }
    };
    r::uv_io_options io_options() {
        r::uv_io_options result;
        result.segment_size = 4096;
        result.disk_retry = 1;
        return result;
    }
    void capacity_contracts() {
        const r::size_t boundary = 32 * 1024 * 1024;
        r::vector<r::size_t> sizes{
            4096,         boundary - 1024, boundary - 512,  boundary - 1, boundary,
            boundary + 1, boundary + 512,  boundary + 1024, boundary * 2, boundary * 2 + 1024};
        if constexpr (r::numeric_limits<r::size_t>::digits > 32) {
            sizes.push_back(static_cast<r::size_t>(r::numeric_limits<r::int64_t>::max() / 2));
            sizes.push_back(static_cast<r::size_t>(r::numeric_limits<r::int64_t>::max() - 4095));
        }
        bool matched = true;
        for (bool active : {false, true}) {
            directory cpp;
            r::vector<r::uint16_t> expected;
            for (const auto size : sizes) {
                r::size_t bytes = size + size;
                if (active)
                    bytes += size - std::min<r::size_t>(size, 512);
                expected.push_back(static_cast<r::uint16_t>(bytes / 1024));
            }
            loop event_loop;
            auto net = std::make_shared<network>();
            r::uv_io backend{event_loop.value, cpp.path, net, io_options()};
            r::io_state loaded;
            check(!backend.init(1, "one") && !backend.load(loaded) &&
                      !backend.start(
                          1000000, [] {}, [](auto) {}),
                  "C++ capacity backend");
            bool done = false;
            if (active)
                check(!backend.append(1, batch(1),
                                      [&](auto status) {
                                          check(!status, "capacity append completion");
                                          done = true;
                                      }),
                      "capacity append accepted");
            event_loop.until(
                [&] { return (!active || done) && backend.capacity() == (active ? 11 : 8); });
            r::vector<r::uint16_t> actual;
            for (const auto size : sizes) {
                check(!backend.set_segment_size(size),
                      "change reported geometry without reallocating prepared files");
                (void)uv_run(&event_loop.value, UV_RUN_NOWAIT);
                actual.push_back(backend.capacity());
            }
            check(!backend.set_segment_size(4096), "restore geometry before close");
            bool closed = false;
            backend.close([&] { closed = true; });
            event_loop.run();
            check(closed && backend.capacity() == 0 && !backend.last_error(),
                  "capacity close drains native work");
            for (r::size_t i = 0; i < sizes.size(); ++i) {
                std::cout << "active=" << active << " segment=" << sizes[i]
                          << " expected=" << expected[i] << " actual=" << actual[i] << '\n';
                matched = matched && actual[i] == expected[i];
            }
            // Runtime geometry changes must not allocate enormous files.
            for (const auto& file : r::fs::directory_iterator{cpp.path})
                check(r::fs::file_size(file.path()) <= 4096,
                      "capacity sampling keeps original small allocations");
        }
        check(matched, "all native capacity samples match the capacity contract");
    }
    void runtime_settings() {
        for (bool recovery : {false, true}) {
            directory dir;
            loop event_loop;
            auto net = std::make_shared<network>();
            auto options = io_options();
            options.auto_recovery = !recovery;
            r::uv_io backend{event_loop.value, dir.path, net, options};
            check(backend.set_block_size(513) == r::errc::invalid && !backend.set_block_size(4096),
                  "validate block override");
            check(!backend.set_segment_size(8192) && !backend.set_snapshot_compression(true) &&
                      !backend.set_snapshot_compression(false),
                  "pre-init settings and no-op compression");
            backend.set_connect_retry_delay(7);
            check(net->connect_retry == 7, "forward retry before initialization");
            check(!backend.init(1, "one") && !backend.set_block_size(0),
                  "reset block to probe result before load");
            backend.set_auto_recovery(recovery);
            {
                std::ofstream damaged{dir.path / "open-99", std::ios::binary};
                const r::array<char, 8> invalid{99, 0, 0, 0, 0, 0, 0, 0};
                damaged.write(invalid.data(), invalid.size());
            }
            r::io_state loaded;
            loaded.term = 77;
            const auto status = backend.load(loaded);
            if (recovery) {
                check(!status && loaded.term == 0, "live auto-recovery permits quarantine");
                check(backend.set_block_size(512) == r::errc::busy,
                      "open writer keeps immutable alignment");
                check(backend.set_segment_size(1) == r::errc::invalid &&
                          !backend.set_segment_size(16384),
                      "live segment size propagation");
                check(backend.capacity() == 32,
                      "file coordinator and writer capacity see new geometry");
                backend.set_disk_retry(0);
                backend.set_connect_retry_delay(3);
                check(net->connect_retry == 3, "forward retry after load");
            } else
                check(status == r::errc::corrupt && loaded.term == 77,
                      "disabled recovery rejects corrupt input and preserves output");
            bool closed = false;
            backend.close([&] { closed = true; });
            event_loop.run();
            check(closed && backend.set_segment_size(8192) == r::errc::shutdown &&
                      backend.set_block_size(0) == r::errc::shutdown &&
                      !backend.set_snapshot_compression(true),
                  "closed settings contracts");
        }
        directory dir;
        loop event_loop;
        std::atomic<unsigned> attempts{0};
        auto storage = disk(dir, [&](auto operation, auto name) -> r::error_code {
            if (operation == d::file_operation::write && name.starts_with("snapshot-") &&
                attempts.fetch_add(1) == 0)
                return r::errc::no_space;
            return {};
        });
        auto options = settings();
        options.log.disk_retry = 60000;
        d::file_backend files{event_loop.value, storage, options};
        check(!files.open(1), "open snapshot retry settings");
        files.set_disk_retry(1);
        put(event_loop, files, 0, 10);
        check(attempts >= 2, "live file retry setting reaches snapshot preparation");
        close(event_loop, files);

        struct reentrant_network : network {
            r::function<void()> on_setting;
            r::weak<reentrant_network> observer;
            bool* alive = nullptr;
            void set_connect_retry_delay(unsigned) noexcept override {
                on_setting();
                *alive = !observer.expired();
            }
        };
        auto net = std::make_shared<reentrant_network>();
        r::weak<reentrant_network> observer = net;
        r::shared<r::uv_io> backend;
        bool retained = false;
        net->observer = observer;
        net->alive = &retained;
        net->on_setting = [&] { backend.reset(); };
        backend = std::make_shared<r::uv_io>(event_loop.value, dir.path, net);
        net.reset();
        backend->set_connect_retry_delay(1);
        check(!backend && retained && observer.expired(),
              "custom policy survives facade destruction until setter returns");
    }

    void lifetime() {
        for (unsigned mode = 0; mode < 8; ++mode) {
            directory dir;
            loop event_loop;
            auto net = std::make_shared<network>();
            auto backend =
                std::make_shared<r::uv_io>(event_loop.value, dir.path, net, io_options());
            if (mode == 0)
                net->init_error = r::errc::io_error;
            if (mode == 1)
                net->start_error = r::errc::io_error;
            const auto initialized = backend->init(1, "one");
            if (mode == 0)
                check(initialized == r::errc::io_error, "transport init failure");
            else {
                check(!initialized && !backend->bootstrap(configuration()), "initialize/bootstrap");
                r::io_state state;
                check(!backend->load(state) && state.entries.size() == 1, "load actual bootstrap");
                auto marker = std::make_shared<unsigned>(7);
                r::weak<unsigned> observer = marker;
                bool ticked = false;
                auto status = backend->start(
                    1,
                    [&, marker] {
                        if (mode == 5)
                            throw std::runtime_error("tick callback");
                        if (mode == 2) {
                            backend->close(
                                [&] { check(ticked, "active tick completes before close"); });
                            backend.reset();
                        }
                        check(*marker == 7, "tick capture survives close and owner destruction");
                        ticked = true;
                    },
                    [mode](auto) {
                        if (mode == 6)
                            throw std::runtime_error("receive callback");
                    });
                marker.reset();
                if (mode == 1)
                    check(status == r::errc::io_error, "transport start failure");
                else {
                    check(!status, "start");
                    if (mode == 2) {
                        event_loop.run();
                        check(ticked && observer.expired() && net->closes == 1,
                              "tick subscription drained");
                        continue;
                    }
                    if (mode >= 5) {
                        if (mode == 6)
                            net->receive(r::message{});
                        if (mode == 7)
                            check(!backend->append(
                                      2, batch(1),
                                      [](auto) { throw std::runtime_error("append callback"); }),
                                  "throwing completion accepted");
                        event_loop.until([&] { return net->closes != 0; });
                        bool closed = false;
                        backend->close([&] { closed = true; });
                        event_loop.run();
                        check(closed && backend->last_error() == r::errc::io_error &&
                                  observer.expired(),
                              "callback exceptions initiate full backend close");
                        continue;
                    }
                    if (mode == 3)
                        net->send_error = r::errc::no_connection;
                    auto message = std::make_shared<r::message>();
                    r::weak<const r::message> weak = message;
                    unsigned calls = 0;
                    auto status = backend->send(message, [&](auto status) {
                        check(status == r::errc::canceled && !weak.expired(),
                              "send retained through cancellation");
                        ++calls;
                    });
                    message.reset();
                    if (mode == 3)
                        check(status == r::errc::no_connection && calls == 0 && weak.expired(),
                              "immediate send failure releases message without callback");
                    else
                        check(!status && !weak.expired(), "accepted send retains message");
                    bool closed = false;
                    backend->close([&] {
                        check(calls == (mode == 3 ? 0U : 1U) && weak.expired(),
                              "sends release before io close");
                        closed = true;
                    });
                    backend.reset();
                    event_loop.run();
                    check(closed && net->closes == 1, "io close completes");
                    continue;
                }
            }
            bool closed = false;
            backend->close([&] { closed = true; });
            backend.reset();
            event_loop.run();
            check(closed && net->closes == 1, "failed init/start closes resources");
        }
    }
    struct machine : r::state_machine {
        r::uint64_t count = 0;
        unsigned finalized = 0, restores = 0;
        r::error_code apply(r::view<const r::byte>, r::shared<void>&) override {
            ++count;
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>& output) override {
            r::buffer value{8};
            std::memcpy(value.data(), &count, 8);
            output.push_back(r::shared_buffer{std::move(value)});
            return {};
        }
        void snapshot_finalize(r::vector<r::shared_buffer>& buffers) noexcept override {
            buffers.clear();
            ++finalized;
        }
        r::error_code restore(r::shared_buffer value) override {
            if (value.size() != 8)
                return r::errc::corrupt;
            std::memcpy(&count, value.bytes().data(), 8);
            ++restores;
            return {};
        }
    };
    void node() {
        directory dir;
        for (unsigned pass = 0; pass < 2; ++pass) {
            loop event_loop;
            auto net = std::make_shared<network>();
            auto backend =
                std::make_shared<r::uv_io>(event_loop.value, dir.path, net, io_options());
            auto fsm = std::make_shared<machine>();
            r::node_options options;
            options.capacity_threshold = 0;
            options.snapshot_threshold = 4;
            options.snapshot_trailing = 2;
            r::node value{1, "one", backend, fsm, options};
            if (pass == 0)
                check(!value.bootstrap(configuration()), "node bootstrap");
            check(!value.start(), "node startup/restore");
            for (unsigned i = 0, n = pass ? 1 : 12; i < n; ++i) {
                bool done = false;
                check(!value.apply(r::buffer{8},
                                   [&](auto status, auto) {
                                       check(!status, "node apply");
                                       done = true;
                                   }),
                      "node command accepted");
                event_loop.until([&] { return done; });
            }
            check(fsm->count == (pass ? 13U : 12U), "snapshot restore plus suffix replay");
            if (pass == 0)
                event_loop.until([&] { return fsm->finalized != 0; });
            else
                check(fsm->restores == 1, "restart restored the actual persisted snapshot");
            bool closed = false;
            check(!value.close([&] { closed = true; }), "node close");
            event_loop.run();
            check(closed && !backend->last_error(), "public node drains native io");
            auto storage = disk(dir);
            r::vector<d::snapshot_file> snapshots;
            check(!storage->list_snapshots(snapshots) && !snapshots.empty(),
                  "public node wrote actual snapshots");
        }
    }
    void capacity_admission() {
        directory dir;
        loop event_loop;
        auto net = std::make_shared<network>();
        auto backend = std::make_shared<r::uv_io>(event_loop.value, dir.path, net, io_options());
        auto fsm = std::make_shared<machine>();
        r::node_options options;
        options.capacity_threshold = 0;
        r::node value{1, "one", backend, fsm, options};
        check(!value.bootstrap(configuration()) && !value.start(),
              "capacity admission single voter");
        event_loop.until(
            [&] { return value.state() == r::node_state::leader && backend->capacity() >= 8; });
        check(!backend->set_segment_size(32 * 1024 * 1024), "capacity boundary geometry");
        check(backend->capacity() < 40000,
              "native capacity preserves the C sixteen-bit conversion");
        value.set_capacity_threshold(40000);
        const auto before = value.last_index();
        unsigned callbacks = 0;
        check(value.apply(r::buffer{8}, [&](auto, auto) { ++callbacks; }) == r::errc::no_space,
              "capacity threshold rejects a wrapped capacity without saturation");
        check(callbacks == 0 && value.last_index() == before && fsm->count == 0 && !value.error(),
              "immediate no-space preserves log/FSM and never calls completion");
        check(!backend->set_segment_size(4096) && backend->capacity() >= 8,
              "restore capacity geometry");
        value.set_capacity_threshold(1);
        check(!value.apply(r::buffer{8},
                           [&](auto status, auto) {
                               check(!status, "command completes after capacity recovery");
                               ++callbacks;
                           }),
              "positive reported capacity permits submission");
        event_loop.until([&] { return callbacks == 1; });
        check(value.last_index() == before + 1 && fsm->count == 1,
              "recovery commits/applies exactly one command");
        bool closed = false;
        check(!value.close([&] { closed = true; }), "capacity admission close");
        event_loop.run();
        check(closed && !backend->last_error(), "admission close drains native resources");
    }
    void limits() {
        directory dir;
        {
            loop event_loop;
            auto storage = disk(dir);
            d::file_backend files{event_loop.value, storage, settings()};
            check(!files.open(1), "open");
            put(event_loop, files, 0, 50);
            close(event_loop, files);
        }
        loop event_loop;
        auto net = std::make_shared<network>();
        auto options = io_options();
        options.max_snapshot_size = 8;
        r::uv_io backend{event_loop.value, dir.path, net, options};
        check(!backend.init(1, "one"), "init with limit");
        r::io_state output;
        output.start_index = 77;
        check(backend.load(output) == r::errc::too_big && output.start_index == 77,
              "startup snapshot limit preserves output");
        bool done = false;
        backend.close([&] { done = true; });
        event_loop.run();
        check(done, "close failed load");
    }
    void io_allocations() {
        for (unsigned kind = 0; kind < 4; ++kind) {
            bool failed = false, succeeded = false;
            for (std::ptrdiff_t position = 0; position < 55; ++position) {
                directory dir;
                loop event_loop;
                const auto before = descriptors();
                {
                    auto net = std::make_shared<network>();
                    r::uv_io backend{event_loop.value, dir.path, net, io_options()};
                    if (kind)
                        check(!backend.init(1, "one"), "init before injection");
                    r::io_state state;
                    if (kind >= 2)
                        check(!backend.load(state), "load before injection");
                    if (kind == 3)
                        check(
                            !backend.start(1000000, [] {}, [](auto) {}), "start before injection");
                    auto value = std::make_shared<r::message>();
                    r::error_code status;
                    unsigned calls = 0;
                    r::completion completed = [&](auto error) {
                        check(error == r::errc::canceled, "pending send canceled");
                        ++calls;
                    };
                    allocation_failure::after(position);
                    if (kind == 0)
                        status = backend.init(1, "one");
                    else if (kind == 1)
                        status = backend.load(state);
                    else if (kind == 2)
                        status = backend.start(1000000, [] {}, [](auto) {});
                    else
                        status = backend.send(value, completed);
                    allocation_failure::after(-1);
                    if (status) {
                        check(status == r::errc::out_of_memory,
                              "public io allocation error mapping");
                        failed = true;
                        check(calls == 0, "immediate error has no callback");
                    } else
                        succeeded = true;
                    bool done = false;
                    r::close_callback callback = [&] { done = true; };
                    allocation_failure::after(0);
                    backend.close(std::move(callback));
                    allocation_failure::after(-1);
                    event_loop.run();
                    check(done && calls == (kind == 3 && !status ? 1U : 0U),
                          "public io close drains after allocation failure");
                }
                check(descriptors() == before, "public io allocation sweep descriptor cleanup");
            }
            check(failed && succeeded, "public io sweep covers failure and success");
        }
    }
    struct cluster_network;
    struct bus {
        struct delivery {
            r::server_id from;
            r::shared<const r::message> value;
            r::completion callback;
        };
        r::map<r::server_id, r::borrowed<cluster_network>> endpoints;
        r::list<delivery> pending;
        r::server_id isolated = 0;
        unsigned installed = 0;
        void pump();
    };
    struct cluster_network : r::transport {
        bus& link;
        r::server_id id = 0;
        r::function<void(r::message)> receive;
        explicit cluster_network(bus& link) : link(link) {}
        r::error_code init(r::server_id value, r::string_view) override {
            id = value;
            link.endpoints[id] = this;
            return {};
        }
        r::error_code start(r::function<void(r::message)> value) override {
            receive = std::move(value);
            return {};
        }
        r::error_code send(r::shared<const r::message> value, r::completion callback) override {
            link.pending.push_back({id, std::move(value), std::move(callback)});
            return {};
        }
        void close(r::close_callback callback) noexcept override {
            receive = {};
            link.endpoints.erase(id);
            for (auto it = link.pending.begin(); it != link.pending.end();) {
                if (it->from != id) {
                    ++it;
                    continue;
                }
                auto operation = std::move(*it);
                it = link.pending.erase(it);
                operation.callback(r::errc::canceled);
            }
            callback();
        }
    };
    void bus::pump() {
        for (unsigned step = 0; !pending.empty() && step < 200; ++step) {
            auto operation = std::move(pending.front());
            pending.pop_front();
            const auto target = endpoints.find(operation.value->server_id);
            if (target == endpoints.end() || isolated == operation.from ||
                isolated == operation.value->server_id) {
                operation.callback(r::errc::no_connection);
                continue;
            }
            r::encoded_message encoded;
            check(!r::encode_message(*operation.value, encoded), "cluster encode");
            r::size_t size = encoded.header.size();
            for (const auto& part : encoded.payload)
                size += part.size();
            r::buffer bytes{size};
            auto cursor = std::ranges::copy(encoded.header.bytes(), bytes.bytes().begin()).out;
            for (const auto& part : encoded.payload)
                cursor = std::ranges::copy(part.bytes(), cursor).out;
            r::message message;
            r::size_t consumed = 0;
            check(!r::decode_message(r::shared_buffer{std::move(bytes)}, message, consumed) &&
                      consumed == size,
                  "cluster decode");
            message.server_id = operation.from;
            message.server_address = std::to_string(operation.from);
            if (std::holds_alternative<r::install_snapshot>(message.payload))
                ++installed;
            auto receive = target->second->receive;
            operation.callback({});
            if (receive)
                receive(std::move(message));
        }
    }
    void cluster() {
        loop event_loop;
        bus link;
        r::array<directory, 3> directories;
        r::array<r::shared<r::uv_io>, 3> backends;
        r::array<r::shared<machine>, 3> machines;
        r::array<r::owned<r::node>, 3> nodes;
        r::configuration members;
        for (unsigned i = 0; i < 3; ++i)
            check(!members.add(i + 1, std::to_string(i + 1), r::role::voter), "cluster member");
        for (unsigned i = 0; i < 3; ++i) {
            backends[i] =
                std::make_shared<r::uv_io>(event_loop.value, directories[i].path,
                                           std::make_shared<cluster_network>(link), io_options());
            machines[i] = std::make_shared<machine>();
            r::node_options options;
            options.capacity_threshold = 0;
            options.heartbeat_timeout = 20;
            options.election_timeout = 1000;
            options.snapshot_threshold = 4;
            options.snapshot_trailing = 1;
            nodes[i] = std::make_unique<r::node>(i + 1, std::to_string(i + 1), backends[i],
                                                 machines[i], options);
            check(!nodes[i]->bootstrap(members) && !nodes[i]->start(), "start real-disk cluster");
        }
        auto leader = [&]() {
            for (unsigned i = 0; i < 3; ++i) {
                if (link.isolated == i + 1 || nodes[i]->state() != r::node_state::leader)
                    continue;
                const auto info = nodes[i]->leader();
                if (info && info->id == i + 1)
                    return i;
            }
            return 3U;
        };
        event_loop.until([&] {
            link.pump();
            return leader() != 3;
        });
        auto primary = leader();
        link.isolated = (primary + 1) % 3 + 1;
        for (unsigned i = 0; i < 16; ++i) {
            if (i == 8) {
                unsigned target = 0;
                while (target == primary || target + 1 == link.isolated)
                    ++target;
                check(!nodes[primary]->transfer(target + 1),
                      "transfer leadership within durable majority");
                event_loop.until([&] {
                    link.pump();
                    return nodes[target]->state() == r::node_state::leader;
                });
            }
            event_loop.until([&] {
                link.pump();
                return leader() != 3;
            });
            primary = leader();
            for (const auto& node : nodes)
                check(!node->error(), "nodes healthy before command submission");
            bool done = false;
            const auto status = nodes[primary]->apply(r::buffer{8}, [&](auto error, auto) {
                check(!error, "majority persists command");
                done = true;
            });
            if (status) {
                std::cerr << "cluster primary=" << primary + 1 << " command=" << i
                          << " status=" << status.message() << '\n';
                for (unsigned j = 0; j < 3; ++j)
                    std::cerr << "node=" << j + 1
                              << " state=" << static_cast<unsigned>(nodes[j]->state())
                              << " term=" << nodes[j]->current_term()
                              << " io=" << backends[j]->last_error().message()
                              << " node=" << nodes[j]->error_message() << '\n';
            }
            check(!status, "submit majority command");
            event_loop.until([&] {
                link.pump();
                return done;
            });
        }
        event_loop.until([&] {
            link.pump();
            return machines[primary]->finalized >= 2;
        });
        check(machines[link.isolated - 1]->count < 16, "isolated node is behind compacted leader");
        link.isolated = 0;
        event_loop.until([&] {
            link.pump();
            return std::ranges::all_of(machines, [](const auto& fsm) { return fsm->count == 16; });
        });
        check(link.installed != 0, "lagging node receives snapshot loaded from actual disk");
        event_loop.until([&] {
            link.pump();
            return leader() != 3;
        });
        primary = leader();
        bool done = false;
        check(!nodes[primary]->apply(r::buffer{8},
                                     [&](auto error, auto) {
                                         check(!error, "post-install command");
                                         done = true;
                                     }),
              "submit after catchup");
        event_loop.until([&] {
            link.pump();
            return done &&
                   std::ranges::all_of(machines, [](const auto& fsm) { return fsm->count == 17; });
        });
        for (const auto& node : nodes)
            check(!node->error(), "all node drivers remain healthy after transfer/install");
        unsigned closed = 0;
        for (auto& node : nodes)
            check(!node->close([&] { ++closed; }), "close cluster node");
        event_loop.run();
        check(closed == 3 && link.pending.empty(),
              "cluster closes all disk and transport requests");
        for (unsigned i = 0; i < 3; ++i) {
            check(!backends[i]->last_error(), "cluster disk backend healthy");
            auto storage = disk(directories[i]);
            r::io_state state;
            check(!storage->load(state) && state.snapshot && state.term != 0,
                  "all cluster stores recover snapshot/log/term");
        }
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected suite");
        {
            loop event_loop;
            uv_work_t work{};
            check(uv_queue_work(
                      &event_loop.value, &work, [](auto*) {}, [](auto*, int) {}) == 0,
                  "warm worker runtime");
            event_loop.run();
        }
        r::string_view suite{argv[1]};
        if (suite == "file_snapshots")
            snapshots();
        else if (suite == "file_snapshot_truncate")
            snapshot_truncate();
        else if (suite == "file_snapshot_order")
            ordering();
        else if (suite == "file_snapshot_failure")
            failures();
        else if (suite == "file_snapshot_close")
            closing();
        else if (suite == "file_snapshot_allocation")
            allocations();
        else if (suite == "uv_io_lifetime")
            lifetime();
        else if (suite == "uv_io_node")
            node();
        else if (suite == "uv_io_limits")
            limits();
        else if (suite == "uv_io_allocation")
            io_allocations();
        else if (suite == "uv_io_cluster")
            cluster();
        else if (suite == "uv_io_settings")
            runtime_settings();
        else if (suite == "uv_io_capacity_contracts")
            capacity_contracts();
        else if (suite == "uv_io_capacity_admission")
            capacity_admission();
        else if (suite == "file_diagnostics")
            diagnostics();
        else
            throw std::runtime_error("unknown suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
