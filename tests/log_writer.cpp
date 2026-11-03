#include "log_writer.hpp"
#include "segment_buffer.hpp"
#include <raftpp/node.hpp>
#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <iostream>
#include <random>
#include <source_location>
#include <thread>
#include <uv.h>
#include <unistd.h>
#include "memory_resource.hpp"

namespace r = raftpp;
namespace d = raftpp::detail;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace {
    std::exception_ptr callback_failure;
    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value) {
            auto failure = std::make_exception_ptr(
                std::runtime_error(r::string{where.file_name()} + ":" +
                                   std::to_string(where.line()) + ": " + r::string{message}));
            if (!callback_failure)
                callback_failure = failure;
            std::rethrow_exception(failure);
        }
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string pattern = "/tmp/raftpp-log-XXXXXX";
            check(mkdtemp(pattern.data()) != nullptr, "temporary directory");
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
                if (callback_failure)
                    try {
                        std::rethrow_exception(callback_failure);
                    } catch (const std::exception& error) {
                        std::cerr << error.what() << '\n';
                    }
                std::cerr << "event loop destroyed with live handles\n";
                std::abort();
            }
        }
        void run() {
            check(uv_run(&value, UV_RUN_DEFAULT) == 0, "loop drain");
            if (callback_failure)
                std::rethrow_exception(callback_failure);
        }
        template <typename F> void until(F&& condition) {
            const auto deadline = uv_hrtime() + 15000000000ULL;
            while (!condition() && uv_hrtime() < deadline) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                if (callback_failure)
                    std::rethrow_exception(callback_failure);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            check(condition(), "condition completed before timeout");
        }
    };
    r::shared<d::disk_storage> storage(const directory& dir, d::file_hook hook = {}) {
        auto value = std::make_shared<d::disk_storage>(dir.path, std::move(hook));
        check(!value->open(), "open storage");
        return value;
    }
    d::log_writer_options options(r::size_t block = 512, r::size_t segment_size = 4096) {
        d::log_writer_options value;
        value.segment_size = segment_size;
        value.disk_retry = 1;
        value.capabilities = d::disk_capabilities{block, false, false};
        return value;
    }
    r::entry entry(r::size_t seed, r::size_t size = 0, r::term term = 2) {
        r::buffer data{size ? size : 8 * (1 + seed % 37)};
        std::ranges::fill(data.bytes(), static_cast<r::byte>(seed % 255));
        return {term, r::entry_type::command, r::shared_buffer{std::move(data)}};
    }
    r::shared<const r::entry_batch> batch(r::size_t n = 1, r::size_t seed = 1, r::size_t size = 0,
                                          r::term term = 2) {
        auto value = std::make_shared<r::entry_batch>();
        for (r::size_t i = 0; i < n; ++i)
            value->push_back(std::make_shared<r::entry>(entry(seed + i, size, term)));
        return value;
    }
    r::vector<r::string> names(const directory& dir) {
        r::vector<r::string> result;
        for (const auto& file : r::fs::directory_iterator{dir.path})
            result.push_back(file.path().filename().native());
        std::ranges::sort(result);
        return result;
    }
    r::size_t descriptors() {
        return static_cast<r::size_t>(
            std::distance(r::fs::directory_iterator{"/proc/self/fd"}, r::fs::directory_iterator{}));
    }
    void buffers() {
        std::mt19937_64 random{113};
        for (r::size_t block : {512U, 4096U}) {
            d::segment_buffer buffer{block};
            r::vector<r::byte> expected(8);
            expected[0] = r::byte{1};
            check(!buffer.format(), "format both buffers");
            for (unsigned step = 0; step < 120; ++step) {
                if (buffer.size() == 0) {
                    check(!buffer.format(), "new segment format");
                    expected.assign(8, r::byte{});
                    expected[0] = r::byte{1};
                }
                r::vector<r::entry> values;
                for (unsigned i = 0, n = 1 + random() % 8; i < n; ++i)
                    values.push_back(entry(step + i, 8 * (1 + random() % 150)));
                r::buffer record;
                check(!r::encode_record(values, record) && !buffer.append(record.bytes()),
                      "append record");
                expected.insert(expected.end(), record.bytes().begin(), record.bytes().end());
                r::shared_buffer frozen;
                check(!buffer.freeze(frozen) &&
                          reinterpret_cast<r::uintptr_t>(frozen.bytes().data()) % block == 0,
                      "aligned immutable write buffer");
                const auto used = expected.size();
                const auto rounded = (used + block - 1) / block * block;
                check(buffer.size() == used && frozen.size() == rounded &&
                          std::ranges::equal(frozen.bytes().first(used), expected) &&
                          std::ranges::all_of(frozen.bytes().subspan(used),
                                              [](auto value) { return value == r::byte{}; }),
                      "staging preserves records and zero-pads to block boundary");
                auto preserved = r::buffer::copy(frozen.bytes());
                if (used >= block) {
                    const auto retain = used % block ? used / block : 0;
                    check(!buffer.reset(retain), "retain last partial block");
                    if (retain)
                        expected.erase(expected.begin(), expected.begin() + retain * block);
                    else
                        expected.clear();
                }
                check(std::ranges::equal(frozen.bytes(), preserved.bytes()),
                      "inflight bytes survive staging reset");
            }
            r::shared_buffer output = r::shared_buffer::copy(r::array{r::byte{7}});
            if (buffer.size() == 0)
                check(!buffer.format(), "failure buffer format");
            allocation_failure::after(0);
            auto status = buffer.freeze(output);
            allocation_failure::after(-1);
            check(status == r::errc::out_of_memory && output.size() == 1 &&
                      output.bytes()[0] == r::byte{7},
                  "freeze allocation failure preserves output");
        }
        d::segment_buffer aliased{512};
        check(!aliased.format(), "self-append format");
        const r::array<r::entry, 1> value{entry(4, 600)};
        r::buffer record;
        check(!r::encode_record(value, record) && !aliased.append(record.bytes()),
              "self-append setup");
        const auto source = aliased.bytes().subspan(8);
        check(!aliased.append(source) &&
                  std::ranges::equal(aliased.bytes().subspan(8, record.size()), record.bytes()) &&
                  std::ranges::equal(aliased.bytes().subspan(8 + record.size()), record.bytes()),
              "aliased append survives arena growth");
    }
    void probe() {
        directory dir;
        auto disk = storage(dir);
        d::disk_capabilities actual;
        const auto before = descriptors();
        check(!disk->probe(actual), "C++ native capability probe");
        check(actual.block_size >= 512 && (actual.block_size & (actual.block_size - 1)) == 0 &&
                  (!actual.async || actual.direct),
              "native capability geometry and dependencies");
        std::cout << "native direct=" << actual.direct << " block=" << actual.block_size
                  << " async=" << actual.async << '\n';
        check(descriptors() == before && names(dir).empty(),
              "probe cleans file/context/descriptor resources");
        if (actual.direct)
            for (unsigned mode = 0; mode < 5; ++mode) {
                d::disk_capabilities output{999, false, false};
                auto status = disk->probe(output, [=](auto operation, auto value) {
                    if (operation == d::writer_operation::setup && mode == 0)
                        return r::int64_t{-EIO};
                    if (operation == d::writer_operation::submit_nowait && mode == 1)
                        return r::int64_t{-EOPNOTSUPP};
                    if (operation == d::writer_operation::poll_result && mode == 2)
                        return r::int64_t{-EAGAIN};
                    if (operation == d::writer_operation::poll_result && mode == 3)
                        return r::int64_t{-EIO};
                    if (operation == d::writer_operation::direct && mode == 4)
                        return r::int64_t{-EIO};
                    return value;
                });
                if (mode == 0 || mode == 4)
                    check(status == r::errc::io_error && output.block_size == 999,
                          "failed probe preserves output");
                else
                    check(!status && output.direct && output.async == (mode == 2),
                          "unsupported/error/EAGAIN capability classification");
                check(descriptors() == before && names(dir).empty(),
                      "failed probe cleans resources");
            }
        unsigned failed = 0, succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 25; ++position) {
            d::disk_capabilities output{999, false, false};
            allocation_failure::after(position);
            auto status = disk->probe(output);
            allocation_failure::after(-1);
            if (status) {
                ++failed;
                check(status == r::errc::out_of_memory && output.block_size == 999,
                      "probe allocation preservation");
            } else
                ++succeeded;
            check(descriptors() == before, "probe allocation cleanup");
        }
        check(failed && succeeded, "probe allocation sweep");
    }
    void append() {
        for (bool automatic : {false, true}) {
            directory dir;
            auto disk = storage(dir);
            loop event_loop;
            d::log_writer writer;
            auto settings = options(512, 4096);
            if (automatic)
                settings.capabilities.reset();
            check(!d::log_writer::open(event_loop.value, disk, 1, settings, writer) &&
                      writer.capacity() == 8192,
                  "prime two unused segments");
            r::vector<r::entry> values;
            unsigned completed = 0;
            for (unsigned i = 0; i < 60; ++i)
                values.push_back(entry(i));
            for (unsigned i = 0; i < values.size(); i += 3) {
                auto input = std::make_shared<r::entry_batch>();
                for (unsigned j = i;
                     j < std::min<unsigned>(i + 3, static_cast<unsigned>(values.size())); ++j)
                    input->push_back(std::make_shared<r::entry>(values[j]));
                check(!writer.append(i + 1, input,
                                     [&, sequence = i / 3](auto status) {
                                         check(!status && completed == sequence,
                                               "append order and success");
                                         ++completed;
                                     }),
                      "append to segment coordinator");
            }
            event_loop.until([&] { return completed == 20; });
            check(writer.next_index() == 61 && !writer.last_error(), "append next index");
            check(!writer.close(), "append close");
            event_loop.run();
            check(writer.closed(), "append shutdown complete");
            for (const auto& filename : names(dir))
                check(!filename.starts_with("open-"),
                      "close finalizes segments and removes prepared pool files");
            r::io_state restored;
            check(!disk->load(restored) && restored.entries.size() == 60,
                  "reload appended records");
            for (unsigned i = 0; i < 60; ++i)
                check(std::ranges::equal(restored.entries[i].payload.bytes(),
                                         values[i].payload.bytes()),
                      "reloaded payload");
        }
        directory dir;
        auto disk = storage(dir);
        loop event_loop;
        d::log_writer writer;
        check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer),
              "oversized batch open");
        bool completed = false;
        check(!writer.append(1, batch(2, 1, 8192),
                             [&](auto status) {
                                 check(!status, "oversized record completion");
                                 completed = true;
                             }),
              "batch larger than initial segment allocation");
        event_loop.until([&] { return completed; });
        check(!writer.close(), "oversized close");
        event_loop.run();
        r::io_state loaded;
        check(!disk->load(loaded) && loaded.entries.size() == 2 &&
                  loaded.entries[0].payload.size() == 8192,
              "oversized record persists and reloads");
        directory boundaries;
        auto boundary_disk = storage(boundaries);
        loop boundary_loop;
        d::log_writer boundary_writer;
        check(
            !d::log_writer::open(boundary_loop.value, boundary_disk, 1, options(), boundary_writer),
            "block-boundary writer");
        const r::array<r::size_t, 4> sizes{472, 8, 1400, 32};
        unsigned done = 0;
        for (unsigned i = 0; i < sizes.size(); ++i) {
            check(!boundary_writer.append(i + 1, batch(1, i, sizes[i]),
                                          [&](auto status) {
                                              check(!status, "block boundary completion");
                                              ++done;
                                          }),
                  "block-boundary append");
            boundary_loop.until([&] { return done == i + 1; });
        }
        check(!boundary_writer.close(), "block-boundary close");
        boundary_loop.run();
        check(!boundary_disk->load(loaded) && loaded.entries.size() == 4 &&
                  r::fs::file_size(boundaries.path / names(boundaries)[0]) == 2048,
              "full first block, retained tail, multi-block tail, and exact final boundary");
    }
    void barriers() {
        for (bool blocking : {true, false}) {
            directory dir;
            auto disk = storage(dir);
            loop event_loop;
            d::log_writer writer;
            check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer),
                  "barrier open");
            unsigned before = 0, after = 0;
            bool ready = false, closed = false;
            for (unsigned i = 0; i < 6; ++i)
                check(!writer.append(i + 1, batch(1, i),
                                     [&](auto status) {
                                         check(!status, "old append");
                                         ++before;
                                     }),
                      "old append submit");
            check(!writer.barrier(blocking ? 3 : 7, blocking,
                                  [&](auto status) {
                                      check(!status && before == 6,
                                            "barrier follows old persistence/finalize");
                                      ready = true;
                                  }),
                  "barrier submit");
            check(!writer.append(blocking ? 3 : 7, batch(3, 40, 0, 3),
                                 [&](auto status) {
                                     check(!status, "new append");
                                     ++after;
                                 }),
                  "append after barrier");
            event_loop.until([&] { return ready; });
            if (blocking) {
                check(after == 0, "blocking barrier holds future writes");
                check(!disk->truncate(3), "truncate after barrier ready");
                writer.unblock();
                event_loop.until([&] { return after == 1; });
            } else {
                event_loop.until([&] { return after == 1; });
                check(!writer.close([&] { closed = true; }),
                      "close during active nonblocking barrier");
                check(!closed, "close waits for barrier work");
                writer.unblock();
            }
            if (blocking)
                check(!writer.close([&] { closed = true; }), "barrier close");
            event_loop.run();
            check(closed, "barrier close callback");
            r::io_state loaded;
            check(!disk->load(loaded) && loaded.entries.size() == (blocking ? 5U : 9U),
                  "barrier/truncate reload");
            if (blocking)
                check(loaded.entries[2].term == 3, "truncated suffix replaced at requested index");
        }
        directory dir;
        auto disk = storage(dir);
        loop event_loop;
        d::log_writer writer;
        check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer),
              "grouped barrier open");
        r::vector<unsigned> order;
        check(!writer.barrier(1, false,
                              [&](auto status) {
                                  check(!status, "first grouped barrier");
                                  order.push_back(1);
                              }),
              "first grouped submit");
        check(!writer.barrier(1, true,
                              [&](auto status) {
                                  check(!status, "second grouped barrier");
                                  order.push_back(2);
                              }),
              "second grouped submit");
        check(order == r::vector<unsigned>{1}, "only first ready request fires before unblock");
        writer.unblock();
        check(order == r::vector<unsigned>({1, 2}), "grouped request order");
        writer.unblock();
        check(!writer.close(), "grouped close");
        event_loop.run();
        directory truncated;
        auto truncated_disk = storage(truncated);
        loop truncate_loop;
        d::log_writer truncated_writer;
        check(!d::log_writer::open(truncate_loop.value, truncated_disk, 1, options(),
                                   truncated_writer),
              "asynchronous truncate open");
        unsigned old = 0, newer = 0, truncations = 0;
        for (unsigned i = 0; i < 8; ++i)
            check(!truncated_writer.append(i + 1, batch(1, i),
                                           [&](auto status) {
                                               check(!status, "append before async truncate");
                                               ++old;
                                           }),
                  "append before truncate submit");
        check(!truncated_writer.truncate(4,
                                         [&](auto status) {
                                             check(!status && old == 8,
                                                   "truncate follows old appends");
                                             ++truncations;
                                         }),
              "asynchronous truncate submit");
        check(!truncated_writer.append(4, batch(3, 50, 0, 4),
                                       [&](auto status) {
                                           check(!status && truncations == 1,
                                                 "replacement follows async truncate");
                                           ++newer;
                                       }),
              "append replacement while truncate pending");
        truncate_loop.until([&] { return newer == 1; });
        check(!truncated_writer.close(), "async truncate close");
        truncate_loop.run();
        r::io_state restored;
        check(!truncated_disk->load(restored) && restored.entries.size() == 6 &&
                  restored.entries[3].term == 4,
              "asynchronous truncate persisted replacement");
    }
    void closing() {
        directory dir;
        auto disk = storage(dir);
        loop event_loop;
        d::log_writer writer;
        check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer), "close open");
        unsigned completed = 0;
        r::vector<r::weak<const r::entry>> lifetimes;
        for (unsigned i = 0; i < 8; ++i) {
            auto input = batch();
            lifetimes.push_back((*input)[0]);
            check(!writer.append(i + 1, input,
                                 [&](auto status) {
                                     check(!status || status == r::errc::canceled,
                                           "closing append status");
                                     ++completed;
                                 }),
                  "close pending append");
        }
        bool closed = false;
        check(!writer.close([&] { closed = true; }), "close queued appends");
        event_loop.run();
        check(closed && completed == 8 &&
                  std::ranges::all_of(lifetimes, [](const auto& p) { return p.expired(); }),
              "all accepted requests complete and release");
        for (const auto& name : names(dir))
            check(!name.starts_with("open-"), "no unused/open files after close");
        // Keep a real segment-allocation worker alive across owner destruction.
        directory waiting;
        std::atomic<bool> enabled{false}, entered{false}, release{false};
        auto waiting_disk = storage(waiting, [&](auto operation, auto filename) {
            if (enabled && operation == d::file_operation::allocate &&
                filename.starts_with("open-")) {
                entered = true;
                while (!release.load())
                    std::this_thread::yield();
            }
            return r::error_code{};
        });
        loop pending_loop;
        d::log_writer pending;
        check(!d::log_writer::open(pending_loop.value, waiting_disk, 1, options(512, 512), pending),
              "prepare close open");
        enabled = true;
        unsigned callbacks = 0;
        for (unsigned i = 0; i < 4; ++i)
            check(!pending.append(i + 1, batch(1, i, 1024),
                                  [&](auto status) {
                                      check(!status || status == r::errc::canceled,
                                            "prepare-close append status");
                                      ++callbacks;
                                  }),
                  "prepare-close append");
        pending_loop.until([&] { return entered.load(); });
        pending = d::log_writer{};
        release = true;
        pending_loop.run();
        check(callbacks == 4, "destroyed owner drains pending preparation");
        for (const auto& name : names(waiting))
            check(!name.starts_with("open-"), "destroyed owner removes unused files");
        {
            directory canceled;
            auto canceled_disk = storage(canceled);
            loop canceled_loop;
            d::log_writer canceled_writer;
            check(!d::log_writer::open(canceled_loop.value, canceled_disk, 1, options(),
                                       canceled_writer),
                  "cancel truncate open");
            unsigned appended = 0, truncated = 0;
            check(!canceled_writer.append(1, batch(2),
                                          [&](auto status) {
                                              check(!status, "write before canceled truncate");
                                              ++appended;
                                          }),
                  "cancel truncate append");
            check(!canceled_writer.truncate(1,
                                            [&](auto status) {
                                                check(status == r::errc::canceled,
                                                      "unstarted truncate cancellation");
                                                ++truncated;
                                            }),
                  "cancel truncate submit");
            check(!canceled_writer.close(), "close unstarted truncate");
            canceled_loop.run();
            check(appended == 1 && truncated == 1, "unstarted truncate completes once");
        }
        {
            directory active;
            std::atomic<bool> enabled{false}, entered{false}, release{false};
            auto active_disk = storage(active, [&](auto operation, auto) {
                if (enabled && operation == d::file_operation::read) {
                    entered = true;
                    while (!release.load())
                        std::this_thread::yield();
                }
                return r::error_code{};
            });
            loop active_loop;
            d::log_writer active_writer;
            check(!d::log_writer::open(active_loop.value, active_disk, 1, options(), active_writer),
                  "active truncate open");
            bool appended = false, truncated = false, closed = false;
            check(!active_writer.append(1, batch(4),
                                        [&](auto status) {
                                            check(!status, "active truncate prefix");
                                            appended = true;
                                        }),
                  "active truncate append");
            active_loop.until([&] { return appended; });
            enabled = true;
            check(!active_writer.truncate(3,
                                          [&](auto status) {
                                              check(!status,
                                                    "active truncate finishes during close");
                                              truncated = true;
                                          }),
                  "active truncate submit");
            active_loop.until([&] { return entered.load(); });
            check(!active_writer.close([&] {
                check(truncated, "close follows active truncate work");
                closed = true;
            }) && !closed,
                  "close waits for active truncate");
            release = true;
            active_loop.run();
            enabled = false;
            r::io_state loaded;
            check(closed && !active_disk->load(loaded) && loaded.entries.size() == 2,
                  "active truncate cleanup and durable prefix");
        }
    }
    void memory() {
        struct selection {
            r::shared<r::pmr::memory_resource> previous = r::get_memory_resource();
            ~selection() {
                (void)r::set_memory_resource(std::move(previous));
            }
        } restore;
        for (bool cancel : {false, true}) {
            auto sa = std::make_shared<memory_testing::statistics>();
            auto sb = std::make_shared<memory_testing::statistics>();
            auto sc = std::make_shared<memory_testing::statistics>();
            r::weak<r::pmr::memory_resource> wa, wb, wc;
            {
                directory dir;
                auto disk = storage(dir);
                loop event_loop;
                d::log_writer writer;
                auto a = std::make_shared<memory_testing::resource>(sa);
                wa = a;
                (void)r::set_memory_resource(a);
                a.reset();
                check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer),
                      "resource writer open");
                auto b = std::make_shared<memory_testing::resource>(sb);
                wb = b;
                (void)r::set_memory_resource(b);
                b.reset();
                unsigned appends = 0, barriers = 0;
                auto appended = [&](auto status) {
                    check(!status || (cancel && status == r::errc::canceled),
                          "resource append completes once");
                    ++appends;
                };
                auto ready = [&](auto status) {
                    check(cancel ? status == r::errc::canceled : !status,
                          "resource barrier completion status");
                    ++barriers;
                };
                check(!writer.append(1, batch(), appended), "resource first append");
                check(!writer.barrier(2, true, ready), "resource first barrier");
                auto c = std::make_shared<memory_testing::resource>(sc);
                wc = c;
                (void)r::set_memory_resource(c);
                c.reset();
                check(!writer.append(2, batch(), appended), "resource second append");
                check(!writer.barrier(3, true, ready), "resource second barrier");
                r::reset_memory_resource();
                check(!wa.expired() && !wb.expired() && !wc.expired(),
                      "queues and operations retain all originating resources");
                bool closed = false;
                if (cancel) {
                    const auto allocations = sa->allocated.load();
                    sa->fail_after = 0;
                    check(!writer.close([&] { closed = true; }),
                          "resource close cancels multiple barrier groups");
                    check(sa->allocated == allocations && sa->fail_after == 0,
                          "cancellation splices parent queues without allocation");
                    sa->fail_after = -1;
                } else {
                    event_loop.until([&] { return barriers == 1; });
                    check(appends == 1, "first blocking group holds later append");
                    writer.unblock();
                    event_loop.until([&] { return barriers == 2; });
                    writer.unblock();
                    check(!writer.close([&] { closed = true; }), "resource completed writer close");
                }
                event_loop.run();
                check(closed && appends == 2 && barriers == 2,
                      "all resource requests drain exactly once");
                if (!cancel) {
                    r::io_state loaded;
                    check(!disk->load(loaded) && loaded.entries.size() == 2,
                          "resource changes preserve durable append order");
                }
            }
            check(wa.expired() && wb.expired() && wc.expired(),
                  "closed writer destruction releases all queue resource owners");
            for (const auto& stats : {sa, sb, sc}) {
                check(stats->allocated > 0 && stats->allocated == stats->freed &&
                          stats->live == 0 && stats->destroyed && !stats->mismatch,
                      "native queues free original resources with exact byte counts and alignment");
            }
        }
    }
    void diagnostics() {
        directory dir;
        loop event_loop;
        auto disk = storage(dir);
        auto settings = options();
        std::atomic<unsigned> writes{0};
        settings.writer = [&](auto operation, auto value) {
            return operation == d::writer_operation::worker_result && writes.fetch_add(1) == 0
                       ? r::int64_t{-ENOSPC}
                       : value;
        };
        d::log_writer writer;
        check(!d::log_writer::open(event_loop.value, disk, 1, settings, writer),
              "diagnostic retry open");
        unsigned completed = 0;
        check(!writer.append(1, batch(),
                             [&](auto status) {
                                 check(!status, "retried append still succeeds");
                                 ++completed;
                             }),
              "diagnostic retry submit");
        event_loop.until([&] { return completed == 1; });
        const auto info = writer.error_details();
        check(info.code == r::errc::io_error && info.cause == std::errc::no_space_on_device &&
                  info.text().starts_with("log.append: open-1: write.worker") &&
                  !writer.last_error(),
              "retry diagnostic preserves cause without changing final status");
        check(!writer.close(), "diagnostic retry close");
        event_loop.run();
        check(info.cause == std::errc::no_space_on_device,
              "diagnostic copy survives retry and close");
    }

    void runtime_settings() {
        directory dir;
        loop event_loop;
        auto disk = storage(dir);
        auto settings = options();
        settings.disk_retry = 60000;
        std::atomic<unsigned> writes{0};
        settings.writer = [&](auto op, auto value) {
            return op == d::writer_operation::worker_result && writes.fetch_add(1) == 0
                       ? r::int64_t{-EIO}
                       : value;
        };
        d::log_writer writer;
        check(!d::log_writer::open(event_loop.value, disk, 1, settings, writer),
              "settings writer open");
        const auto capacity = writer.capacity();
        check(writer.set_segment_size(511) == r::errc::invalid &&
                  writer.set_segment_size(0) == r::errc::invalid && writer.capacity() == capacity,
              "invalid geometry preserves capacity");

        struct blocked_worker {
            uv_work_t work{};
            std::atomic<bool> entered{false}, release{false};
            loop& event_loop;
            explicit blocked_worker(loop& value) : event_loop(value) {
                work.data = this;
                check(!uv_queue_work(
                          &value.value, &work,
                          [](auto* native) {
                              auto& self = *static_cast<blocked_worker*>(native->data);
                              self.entered = true;
                              while (!self.release.load())
                                  std::this_thread::sleep_for(r::chrono::milliseconds{1});
                          },
                          [](auto*, int) {}),
                      "block single worker pool");
            }
            ~blocked_worker() {
                release = true;
                event_loop.run();
            }
        } gate{event_loop};
        event_loop.until([&] { return gate.entered.load(); });
        unsigned done = 0;
        check(!writer.append(1, batch(1, 1, 1024),
                             [&](auto status) {
                                 check(!status, "settings first append");
                                 ++done;
                             }),
              "queued append");
        check(!writer.set_segment_size(1536), "change geometry while prepare is queued");
        writer.set_disk_retry(1);
        gate.release = true;
        event_loop.until([&] { return done == 1 && r::fs::exists(dir.path / "open-3"); });
        check(r::fs::file_size(dir.path / "open-3") == 4096,
              "queued prepare retains original allocation size");
        check(!writer.append(2, batch(1, 2, 1024),
                             [&](auto status) {
                                 check(!status, "settings second append");
                                 ++done;
                             }),
              "append after shrink");
        event_loop.until([&] {
            return done == 2 && r::fs::exists(dir.path / "open-4") &&
                   r::fs::file_size(dir.path / "open-4") == 1536;
        });
        check(writes >= 3, "live retry setting applied to failed write");
        check(!writer.close(), "settings writer close");
        event_loop.run();
        check(writer.set_segment_size(4096) == r::errc::shutdown,
              "closed writer rejects geometry change");
        r::io_state loaded;
        check(!disk->load(loaded) && loaded.entries.size() == 2,
              "geometry changes preserve durable log");
    }

    void failures() {
        directory dir;
        std::atomic<int> remaining{3}, attempts{0};
        auto disk = storage(dir, [&](auto op, auto) {
            if (op == d::file_operation::allocate) {
                ++attempts;
                auto left = remaining.load();
                while (left > 0 && !remaining.compare_exchange_weak(left, left - 1)) {
                }
                if (left > 0)
                    return r::make_error_code(r::errc::no_space);
            }
            return r::error_code{};
        });
        loop event_loop;
        d::log_writer writer;
        check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer),
              "retry prepare open");
        unsigned completed = 0;
        check(!writer.append(1, batch(),
                             [&](auto status) {
                                 check(!status, "prepare retry completion");
                                 ++completed;
                             }),
              "prepare retry accepted");
        event_loop.until([&] { return completed == 1; });
        check(attempts >= 4 && remaining == 0,
              "allocation failures retried without losing request");
        check(!writer.close(), "retry prepare close");
        event_loop.run();
        directory writes;
        auto write_disk = storage(writes);
        loop write_loop;
        d::log_writer retried;
        auto settings = options();
        std::atomic<unsigned> calls{0};
        settings.writer = [&](auto op, auto value) {
            if (op == d::writer_operation::worker_result && calls.fetch_add(1) == 0)
                return r::int64_t{-EIO};
            return value;
        };
        check(!d::log_writer::open(write_loop.value, write_disk, 1, settings, retried),
              "retry write open");
        r::vector<unsigned> order;
        for (unsigned i = 0; i < 2; ++i)
            check(!retried.append(i + 1, batch(1, i),
                                  [&, i](auto status) {
                                      check(!status, "write retry result");
                                      order.push_back(i);
                                  }),
                  "write retry submit");
        write_loop.until([&] { return order.size() == 2; });
        check(order == r::vector<unsigned>({0, 1}) && calls >= 3,
              "retry keeps bytes and callback order");
        check(!retried.close(), "retry write close");
        write_loop.run();
        r::io_state loaded;
        check(!write_disk->load(loaded) && loaded.entries.size() == 2,
              "write retry creates no duplicate records");
        directory failed;
        auto failed_disk = storage(failed, [](auto op, auto) {
            return op == d::file_operation::rename ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
        });
        loop failed_loop;
        d::log_writer finalized;
        check(!d::log_writer::open(failed_loop.value, failed_disk, 1, options(), finalized),
              "finalize failure open");
        unsigned ready = 0;
        check(!finalized.append(
                  1, batch(), [](auto status) { check(!status, "write before failed finalize"); }),
              "append before failed finalize");
        check(!finalized.barrier(2, true,
                                 [&](auto status) {
                                     check(status == r::errc::canceled,
                                           "failed finalize cancels pending barrier");
                                     ++ready;
                                 }),
              "failed finalize barrier");
        failed_loop.run();
        check(finalized.closed() && finalized.last_error() == r::errc::io_error && ready == 1,
              "finalize failure closes without leaking barrier");
        d::disk_storage recovered{failed.path};
        check(!recovered.open() && !recovered.load(loaded) && loaded.entries.size() == 1,
              "unrenamed durable segment recovered after failure is removed");
    }
    void allocations() {
        directory dir;
        unsigned failed = 0, succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 35; ++position) {
            const auto before = descriptors();
            {
                loop event_loop;
                auto disk = storage(dir);
                d::log_writer writer;
                auto settings = options();
                allocation_failure::after(position);
                const auto status =
                    d::log_writer::open(event_loop.value, disk, 1, std::move(settings), writer);
                allocation_failure::after(-1);
                if (status) {
                    ++failed;
                    check(status == r::errc::out_of_memory, "log initialization allocation error");
                } else {
                    ++succeeded;
                    check(!writer.close(), "allocation open close");
                }
                event_loop.run();
            }
            check(descriptors() == before && names(dir).empty(),
                  "initialization failure drains handles and prepared files");
        }
        check(failed && succeeded, "initialization allocation sweep");
        failed = 0;
        succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 50; ++position) {
            directory current;
            const auto before = descriptors();
            {
                loop event_loop;
                auto disk = storage(current);
                d::log_writer writer;
                check(!d::log_writer::open(event_loop.value, disk, 1, options(), writer),
                      "append allocation open");
                auto input = batch();
                unsigned completed = 0;
                r::error_code completion_status;
                r::completion callback = [&](auto status) {
                    ++completed;
                    completion_status = status;
                };
                allocation_failure::after(position);
                const auto status = writer.append(1, input, std::move(callback));
                allocation_failure::after(-1);
                if (status) {
                    ++failed;
                    check(status == r::errc::out_of_memory && completed == 0 &&
                              writer.next_index() == 1,
                          "immediate append failure preserves state");
                } else {
                    ++succeeded;
                    if (!writer.closed())
                        event_loop.until([&] { return completed != 0; });
                    check(completed == 1 &&
                              (!completion_status || completion_status == r::errc::out_of_memory),
                          "accepted append completes exactly once after OOM");
                }
                r::close_callback closed = [] {};
                allocation_failure::after(0);
                const auto close_status = writer.close(std::move(closed));
                allocation_failure::after(-1);
                check(!close_status, "coordinator close uses no allocation");
                event_loop.run();
            }
            check(descriptors() == before, "append allocation failure releases descriptors");
        }
        check(failed && succeeded, "append allocation sweep");
    }
    // Single-voter integration adapter: real disk I/O; transport and snapshot
    // operations are outside this test and must not be invoked.
    struct native_backend : r::io {
        r::borrowed<uv_loop_t> event_loop;
        r::shared<d::disk_storage> disk;
        d::log_writer writer;
        native_backend(uv_loop_t& loop, r::shared<d::disk_storage> value)
            : event_loop(&loop), disk(std::move(value)) {}
        r::error_code init(r::server_id, r::string_view) override {
            return {};
        }
        r::error_code bootstrap(const r::configuration& value) override {
            return disk->bootstrap(value);
        }
        r::error_code recover(const r::configuration& value) override {
            return disk->recover(value);
        }
        r::error_code load(r::io_state& value) override {
            if (auto status = disk->load(value))
                return status;
            return d::log_writer::open(*event_loop, disk, value.start_index + value.entries.size(),
                                       options(), writer);
        }
        r::error_code start(unsigned, r::function<void()>, r::function<void(r::message)>) override {
            return {};
        }
        void close(r::close_callback callback) noexcept override {
            (void)writer.close(std::move(callback));
        }
        r::error_code set_term(r::term value) override {
            return disk->set_term(value);
        }
        r::error_code set_vote(r::server_id value) override {
            return disk->set_vote(value);
        }
        r::error_code truncate(r::log_index index) override {
            return writer.truncate(index);
        }
        r::error_code append(r::log_index index, r::shared<const r::entry_batch> value,
                             r::completion callback) override {
            return writer.append(index, std::move(value), std::move(callback));
        }
        r::error_code send(r::shared<const r::message>, r::completion) override {
            return r::errc::no_connection;
        }
        r::error_code snapshot_put(unsigned, r::shared<const r::snapshot>, r::completion) override {
            return r::errc::invalid;
        }
        r::error_code snapshot_get(r::snapshot_callback) override {
            return r::errc::invalid;
        }
        r::uint64_t time() const noexcept override {
            return uv_now(event_loop);
        }
        r::uint32_t random(r::uint32_t first, r::uint32_t) noexcept override {
            return first;
        }
        r::uint16_t capacity() const noexcept override {
            return static_cast<r::uint16_t>(writer.capacity() / 1024);
        }
    };
    struct native_machine : r::state_machine {
        unsigned applied = 0;
        r::error_code apply(r::view<const r::byte> value, r::shared<void>& result) override {
            if (value.size() != 8)
                return r::errc::malformed;
            ++applied;
            result = std::make_shared<unsigned>(applied);
            return {};
        }
        r::error_code restore(r::shared_buffer) override {
            return r::errc::invalid;
        }
        r::error_code snapshot(r::vector<r::shared_buffer>&) override {
            return r::errc::busy;
        }
    };
    void native_node() {
        directory dir;
        r::configuration configuration;
        check(!configuration.add(1, "one", r::role::voter), "native node configuration");
        for (unsigned pass = 0; pass < 2; ++pass) {
            loop event_loop;
            auto disk = storage(dir);
            auto backend = std::make_shared<native_backend>(event_loop.value, disk);
            auto machine = std::make_shared<native_machine>();
            r::node_options settings;
            settings.capacity_threshold = 0;
            r::node node{1, "one", backend, machine, settings};
            if (pass == 0)
                check(!node.bootstrap(configuration), "native node bootstrap");
            check(!node.start(), "native node startup/recovery");
            unsigned completed = 0;
            bool closed = false;
            const unsigned commands = pass == 0 ? 12 : 1;
            for (unsigned i = 0; i < commands; ++i) {
                r::buffer command{8};
                std::ranges::fill(command.bytes(), static_cast<r::byte>(i + 1));
                const auto status =
                    node.apply(std::move(command), [&](auto status, r::shared<void> result) {
                        check(!status && result != nullptr,
                              "native persistence precedes apply completion");
                        ++completed;
                        if (pass == 0 && completed == commands)
                            check(!node.close([&] { closed = true; }),
                                  "native close from apply completion");
                    });
                if (status)
                    std::cerr << "native node pass=" << pass << " command=" << i
                              << " status=" << status.message()
                              << " state=" << static_cast<unsigned>(node.state()) << '\n';
                check(!status, "native node command");
            }
            event_loop.until([&] { return completed == commands; });
            check(machine->applied == (pass == 0 ? 12U : 13U),
                  "native restart replays previous command prefix");
            if (pass != 0)
                check(!node.close([&] { closed = true; }), "native node close");
            event_loop.run();
            check(closed && !backend->writer.last_error(),
                  "native node close drains disk coordinator");
        }
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected log-writer suite");
        {
            loop runtime;
            uv_work_t request{};
            check(uv_queue_work(
                      &runtime.value, &request, [](uv_work_t*) {}, [](uv_work_t*, int) {}) == 0,
                  "runtime init");
            runtime.run();
        }
        const r::string_view suite{argv[1]};
        if (suite == "segment_buffer")
            buffers();
        else if (suite == "disk_probe")
            probe();
        else if (suite == "log_append")
            append();
        else if (suite == "log_barrier")
            barriers();
        else if (suite == "log_close")
            closing();
        else if (suite == "log_failure")
            failures();
        else if (suite == "log_allocation")
            allocations();
        else if (suite == "log_memory")
            memory();
        else if (suite == "native_node")
            native_node();
        else if (suite == "log_settings")
            runtime_settings();
        else if (suite == "log_diagnostics")
            diagnostics();
        else
            throw std::runtime_error("unknown log-writer suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
