#include "writer.hpp"
#include "probe.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <exception>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <uv.h>

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
            r::string pattern = "/tmp/raftpp-writer-XXXXXX";
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
            if (uv_loop_close(&value) != 0)
                std::abort();
        }
        void run() {
            check(uv_run(&value, UV_RUN_DEFAULT) == 0, "drain loop");
            if (callback_failure)
                std::rethrow_exception(callback_failure);
        }
    };
    r::size_t descriptors() {
        return static_cast<r::size_t>(
            std::distance(r::fs::directory_iterator{"/proc/self/fd"}, r::fs::directory_iterator{}));
    }
    d::file_handle file(const r::fs::path& path) {
        d::file_handle value;
        check(!d::file_handle::open(path, O_RDWR | O_CREAT, 0600, value), "file open");
        check(posix_fallocate(value.get(), 0, 65536) == 0, "allocate file");
        return value;
    }
    r::vector<r::shared_buffer> buffers(unsigned n = 2) {
        r::vector<r::shared_buffer> value;
        for (unsigned i = 0; i < n; ++i) {
            r::aligned_buffer part{4096, 4096};
            std::ranges::fill(part.bytes(), static_cast<r::byte>(i + 1));
            value.emplace_back(std::move(part));
        }
        return value;
    }
    r::buffer read(const r::fs::path& path) {
        r::buffer bytes{static_cast<r::size_t>(r::fs::file_size(path))};
        std::ifstream stream{path, std::ios::binary};
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        check(stream.good(), "read written file");
        return bytes;
    }
    bool async_available(const directory& dir) {
        d::disk_capabilities capabilities;
        check(!d::probe_filesystem(dir.path, capabilities), "native capability probe");
        std::cout << "native direct block size=" << capabilities.block_size
                  << ", NOWAIT=" << capabilities.async << '\n';
        return capabilities.direct && capabilities.async;
    }
    void tracing() {
        directory dir;
        for (bool direct : {false, true}) {
            loop event_loop;
            d::file_writer writer;
            auto input = buffers(1);
            r::vector<r::uint32_t> cpp;
            unsigned old = 0;
            d::writer_options options{direct, false, 1, r::tracer{[&](const auto&) { ++old; }}};
            check(!d::file_writer::open(event_loop.value, file(dir.path / "cpp"), options, writer),
                  "traced writer open");
            writer.set_tracer(r::tracer{[&](const auto& event) {
                check(!event.info, "native metric has null data");
                cpp.push_back(event.type);
            }});
            bool done = false, closed = false;
            check(!writer.submit(
                      input, 0,
                      [&](auto status) {
                          check(!status && cpp.size() == 2 && cpp.back() == r::trace_write_complete,
                                "completion metric precedes user callback");
                          done = true;
                          check(!writer.close([&] { closed = true; }), "traced writer close");
                      }),
                  "traced write accepted");
            event_loop.run();
            check(done && closed && old == 0, "runtime tracer replacement");
            check(cpp == r::vector<r::uint32_t>{r::trace_write_submit, r::trace_write_complete},
                  "native submit/completion metric order");
        }
    }
    void basic() {
        directory dir;
        for (bool direct : {false, true}) {
            loop event_loop;
            d::file_writer writer;
            auto input = buffers();
            auto expected = input;
            auto opened = file(dir.path / (direct ? "cpp-direct" : "cpp-buffered"));
            const auto descriptor = opened.get();
            check(!d::file_writer::open(event_loop.value, std::move(opened), {direct, false, 1},
                                        writer),
                  "writer open");
            r::weak<const r::byte> lifetime = input.front().storage();
            unsigned completed = 0;
            bool closed = false;
            check(!writer.submit(input, 4096,
                                 [&](auto status) {
                                     check(!status && !lifetime.expired(),
                                           "worker completion and payload lifetime");
                                     ++completed;
                                     check(!writer.close([&] { closed = true; }),
                                           "close from completion");
                                 }),
                  "submit scatter write");
            input.clear();
            expected.clear();
            check(!lifetime.expired(), "accepted request retains payload");
            event_loop.run();
            check(completed == 1 && closed && lifetime.expired() && writer.closed() &&
                      fcntl(descriptor, F_GETFD) == -1,
                  "completion, final release, and file closure");
            const auto actual = read(dir.path / (direct ? "cpp-direct" : "cpp-buffered"));
            check(actual.size() == 65536, "writes preserve allocated file size");
            for (r::size_t i = 0; i < actual.size(); ++i) {
                const auto expected = i >= 4096 && i < 8192    ? r::byte{1}
                                      : i >= 8192 && i < 12288 ? r::byte{2}
                                                               : r::byte{};
                check(actual.bytes()[i] == expected,
                      "scatter write changes exactly the submitted file range");
            }
        }
        // Resubmission from completion is allowed after removing the old request.
        loop event_loop;
        d::file_writer writer;
        auto input = buffers(1);
        check(!d::file_writer::open(event_loop.value, file(dir.path / "sequence"), {}, writer),
              "sequence open");
        unsigned completed = 0;
        r::completion callback;
        callback = [&](auto status) {
            check(!status, "sequence result");
            if (++completed < 8)
                check(!writer.submit(input, completed * 4096, callback), "reentrant submit");
            else
                check(!writer.close(), "sequence close");
        };
        check(!writer.submit(input, 0, callback), "sequence initial submit");
        event_loop.run();
        check(completed == 8, "all sequence callbacks");
    }
    void asynchronous() {
        directory dir;
        const bool available = async_available(dir);
        // NOWAIT submission EAGAIN always takes the real worker path.
        {
            loop event_loop;
            d::file_writer writer;
            std::atomic<unsigned> workers{0};
            check(!d::file_writer::open(event_loop.value, file(dir.path / "fallback"),
                                        {true, true, 1}, writer,
                                        [&](auto operation, r::int64_t value) {
                                            if (operation == d::writer_operation::submit_nowait)
                                                return r::int64_t{-EAGAIN};
                                            if (operation == d::writer_operation::submit_worker)
                                                ++workers;
                                            return value;
                                        }),
                  "fallback open");
            bool completed = false;
            check(!writer.submit(buffers(), 0,
                                 [&](auto status) {
                                     check(!status, "fallback result");
                                     completed = true;
                                     check(!writer.close(), "fallback close");
                                 }),
                  "fallback submit");
            event_loop.run();
            check(completed && workers == 1, "submission EAGAIN routed to worker");
        }
        if (!available) {
            std::cout << "Native polled cases require a filesystem supporting NOWAIT\n";
            return;
        }
        for (bool fallback : {false, true}) {
            loop event_loop;
            d::file_writer writer;
            unsigned polled = 0;
            std::atomic<unsigned> workers{0};
            unsigned completed = 0;
            check(!d::file_writer::open(event_loop.value,
                                        file(dir.path / (fallback ? "poll-retry" : "poll")),
                                        {true, true, 4}, writer,
                                        [&](auto operation, r::int64_t value) {
                                            if (operation == d::writer_operation::poll_result) {
                                                ++polled;
                                                if (fallback)
                                                    return r::int64_t{-EAGAIN};
                                            }
                                            if (operation == d::writer_operation::submit_worker)
                                                ++workers;
                                            return value;
                                        }),
                  "poll writer open");
            for (unsigned i = 0; i < 4; ++i) {
                check(!writer.submit(buffers(1), i * 4096,
                                     [&](auto status) {
                                         check(!status, "polled result");
                                         if (++completed == 4)
                                             check(!writer.close(), "polled close");
                                     }),
                      "concurrent native poll submit");
            }
            event_loop.run();
            check(completed == 4 && polled > 0 && (!fallback || workers == 4),
                  "all native events handled across EAGAIN fallbacks");
        }
    }
    void closing() {
        directory dir;
        // Hold an actual worker inside a hook, then close and move the owner.
        {
            loop event_loop;
            d::file_writer writer;
            std::atomic<bool> entered{false}, released{false};
            check(!d::file_writer::open(event_loop.value, file(dir.path / "pending-worker"), {},
                                        writer,
                                        [&](auto operation, auto value) {
                                            if (operation == d::writer_operation::submit_worker) {
                                                entered = true;
                                                while (!released.load())
                                                    std::this_thread::yield();
                                            }
                                            return value;
                                        }),
                  "pending worker open");
            auto input = buffers();
            r::weak<const r::byte> lifetime = input[0].storage();
            unsigned completed = 0;
            bool closed = false;
            check(!writer.submit(input, 0,
                                 [&](auto status) {
                                     check(!status && !lifetime.expired(),
                                           "pending worker completion");
                                     ++completed;
                                 }),
                  "pending submit");
            const auto deadline = r::chrono::steady_clock::now() + r::chrono::seconds{5};
            while (!entered.load() && r::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            if (!entered.load())
                released = true;
            check(entered.load(), "worker started before close");
            input.clear();
            d::file_writer moved{std::move(writer)};
            check(!moved.close([&] {
                check(completed == 1 && lifetime.expired(),
                      "close follows worker and payload release");
                closed = true;
            }),
                  "close pending worker");
            (void)uv_run(&event_loop.value, UV_RUN_NOWAIT);
            check(!closed && !lifetime.expired(), "close retains in-flight worker data");
            released = true;
            event_loop.run();
            check(closed && moved.closed(), "pending worker drained after move");
        }
        {
            loop event_loop;
            r::weak<const r::byte> lifetime;
            bool completed = false;
            {
                d::file_writer writer;
                check(
                    !d::file_writer::open(event_loop.value, file(dir.path / "discard"), {}, writer),
                    "discard open");
                auto input = buffers();
                lifetime = input[0].storage();
                check(!writer.submit(input, 0, [&](auto status) { completed = !status; }),
                      "discard submit");
            }
            check(!lifetime.expired(), "discarded owner retains accepted data");
            event_loop.run();
            check(completed && lifetime.expired(), "discarded owner completes and releases");
        }
        if (!async_available(dir))
            return;
        loop event_loop;
        d::file_writer writer;
        check(!d::file_writer::open(event_loop.value, file(dir.path / "pending-poll"),
                                    {true, true, 1}, writer),
              "pending poll open");
        auto input = buffers();
        r::weak<const r::byte> lifetime = input[0].storage();
        bool canceled = false, closed = false;
        check(!writer.submit(input, 0,
                             [&](auto status) {
                                 canceled = status == r::errc::canceled && !lifetime.expired();
                             }),
              "pending poll submit");
        input.clear();
        check(!writer.close([&] {
            check(canceled && lifetime.expired(), "kernel relinquishes data before final close");
            closed = true;
        }),
              "pending poll close");
        event_loop.run();
        check(canceled && closed, "pending poll canceled once after kernel cleanup");
    }
    void diagnostics() {
        directory dir;
        for (auto operation :
             {d::writer_operation::direct, d::writer_operation::eventfd, d::writer_operation::setup,
              d::writer_operation::poll_init, d::writer_operation::poll_start}) {
            loop event_loop;
            d::file_writer writer;
            r::error_info info;
            const bool limit = operation == d::writer_operation::setup;
            const auto status = d::file_writer::open(
                event_loop.value, file(dir.path / "diagnostic-init"), {}, writer,
                [=](auto point, auto value) {
                    return point == operation ? r::int64_t{limit ? -EAGAIN : -EACCES} : value;
                },
                {}, &info);
            check(status == (limit ? r::errc::too_many : r::errc::io_error) &&
                      info.code == status && info.cause.value() == (limit ? EAGAIN : EACCES),
                  "init captures exact cause before asynchronous cleanup");
            event_loop.run();
            check(info.text().find("fd ") != r::string_view::npos,
                  "init diagnostic owns descriptor context after cleanup");
        }
        for (bool nowait : {false, true}) {
            loop event_loop;
            d::file_writer writer;
            const auto point =
                nowait ? d::writer_operation::submit_nowait : d::writer_operation::queue_work;
            check(!d::file_writer::open(
                      event_loop.value, file(dir.path / "diagnostic-submit"), {nowait, nowait, 1},
                      writer,
                      [=](auto op, auto value) { return op == point ? r::int64_t{-EIO} : value; }),
                  "submission diagnostic open");
            unsigned callbacks = 0;
            check(writer.submit(buffers(1), 4096, [&](auto) { ++callbacks; }) ==
                          r::errc::io_error &&
                      callbacks == 0,
                  "failed submit has no callback");
            const auto info = writer.error_details();
            check(info.cause == std::errc::io_error &&
                      info.text().find("offset 4096") != r::string_view::npos,
                  "immediate native submission context");
            check(!writer.close(), "submission diagnostic close");
            event_loop.run();
        }
        {
            loop event_loop;
            d::file_writer writer;
            check(!d::file_writer::open(event_loop.value, file(dir.path / "diagnostic-concurrent"),
                                        {false, false, 4}, writer,
                                        [](auto op, auto value) {
                                            if (op != d::writer_operation::worker_result)
                                                return value;
                                            if (value == 4096)
                                                return r::int64_t{-EIO};
                                            if (value == 8192)
                                                return r::int64_t{-ENOSPC};
                                            if (value == 12288)
                                                return r::int64_t{8};
                                            return r::numeric_limits<r::int64_t>::min();
                                        }),
                  "concurrent diagnostic open");
            r::array<r::error_info, 4> captured{};
            unsigned completed = 0;
            for (unsigned i = 0; i < 4; ++i)
                check(!writer.submit(
                          buffers(i + 1), i * 16384,
                          [&, i](auto status) {
                              const auto info = writer.error_details();
                              check(info.code == status &&
                                        status == (i == 2 ? r::errc::no_space : r::errc::io_error),
                                    "per-request error mapping");
                              check(info.cause.value() == (i == 0   ? EIO
                                                           : i == 1 ? ENOSPC
                                                                    : 0),
                                    "concurrent causes remain paired with requests");
                              check(info.text().find("offset " + std::to_string(i * 16384)) !=
                                        r::string_view::npos,
                                    "concurrent offsets remain paired");
                              captured[i] = info;
                              if (++completed == 4)
                                  check(!writer.close(), "concurrent diagnostic close");
                          }),
                      "concurrent diagnostic submit");
            event_loop.run();
            check(completed == 4 && captured[0].cause == std::errc::io_error &&
                      captured[1].cause == std::errc::no_space_on_device,
                  "diagnostic copies survive all completions/close");
        }
        {
            loop event_loop;
            d::file_writer writer;
            unsigned attempts = 0;
            check(!d::file_writer::open(
                      event_loop.value, file(dir.path / "diagnostic-shutdown"), {}, writer,
                      [&](auto op, auto value) {
                          return op == d::writer_operation::queue_shutdown && attempts++ == 0
                                     ? r::int64_t{-EAGAIN}
                                     : value;
                      }),
                  "shutdown diagnostic open");
            bool closed = false;
            check(!writer.close([&] { closed = true; }), "shutdown diagnostic close accepted");
            event_loop.run();
            check(closed && attempts >= 2 &&
                      writer.error_details().cause == std::errc::resource_unavailable_try_again &&
                      writer.error_details().text().starts_with("uv_queue_work.shutdown"),
                  "shutdown retry preserves original native cause");
        }
        if (async_available(dir)) {
            loop event_loop;
            d::file_writer writer;
            check(!d::file_writer::open(event_loop.value, file(dir.path / "diagnostic-poll"),
                                        {true, true, 1}, writer,
                                        [](auto op, auto value) {
                                            return op == d::writer_operation::poll_result
                                                       ? r::int64_t{-ENOSPC}
                                                       : value;
                                        }),
                  "poll diagnostic open");
            bool completed = false;
            check(!writer.submit(buffers(1), 0,
                                 [&](auto status) {
                                     completed = status == r::errc::io_error &&
                                                 writer.error_details().cause ==
                                                     std::errc::no_space_on_device;
                                     check(!writer.close(), "poll diagnostic close");
                                 }),
                  "poll diagnostic submit");
            event_loop.run();
            check(completed, "native polled result preserves cause");
        } else
            std::cout << "Polled diagnostic case requires native NOWAIT support\n";
    }

    void failures() {
        directory dir;
        for (auto operation :
             {d::writer_operation::direct, d::writer_operation::eventfd, d::writer_operation::setup,
              d::writer_operation::poll_init, d::writer_operation::poll_start}) {
            const auto before = descriptors();
            {
                loop event_loop;
                d::file_writer writer;
                const auto status =
                    d::file_writer::open(event_loop.value, file(dir.path / "init-failure"), {},
                                         writer, [=](auto op, auto value) {
                                             return op == operation ? r::int64_t{-EIO} : value;
                                         });
                check(status == r::errc::io_error && writer.closed(),
                      "partial init failure preserves output");
                event_loop.run();
            }
            check(descriptors() == before, "partial init closes every fd and handle");
        }
        {
            loop event_loop;
            d::file_writer writer;
            check(d::file_writer::open(event_loop.value, file(dir.path / "aio-limit"), {}, writer,
                                       [](auto operation, auto value) {
                                           return operation == d::writer_operation::setup
                                                      ? r::int64_t{-EAGAIN}
                                                      : value;
                                       }) == r::errc::too_many,
                  "AIO limit mapping");
            event_loop.run();
        }
        for (auto operation :
             {d::writer_operation::queue_work, d::writer_operation::submit_nowait}) {
            loop event_loop;
            d::file_writer writer;
            const bool async = operation == d::writer_operation::submit_nowait;
            check(!d::file_writer::open(event_loop.value, file(dir.path / "submit-failure"),
                                        {async, async, 1}, writer,
                                        [=](auto op, auto value) {
                                            return op == operation ? r::int64_t{-EIO} : value;
                                        }),
                  "immediate failure open");
            bool completed = false;
            auto input = buffers();
            r::weak<const r::byte> lifetime = input[0].storage();
            check(writer.submit(input, 0, [&](auto) { completed = true; }) == r::errc::io_error,
                  "immediate submission failure");
            input.clear();
            check(!completed && lifetime.expired() && !writer.close(),
                  "no completion or retained payload on immediate failure");
            event_loop.run();
        }
        for (r::int64_t result : {r::int64_t{-EIO}, r::int64_t{8}}) {
            loop event_loop;
            d::file_writer writer;
            check(!d::file_writer::open(
                      event_loop.value, file(dir.path / "result-failure"), {}, writer,
                      [=](auto operation, auto value) {
                          return operation == d::writer_operation::worker_result ? result : value;
                      }),
                  "result failure open");
            bool completed = false;
            check(!writer.submit(buffers(), 0,
                                 [&](auto status) {
                                     completed = true;
                                     check(status ==
                                               (result < 0 ? r::errc::io_error : r::errc::no_space),
                                           "negative vs short-write mapping");
                                     check(!writer.close(), "result failure close");
                                 }),
                  "result failure submit");
            event_loop.run();
            check(completed, "failure delivered exactly once");
        }
        {
            loop event_loop;
            d::file_writer writer;
            unsigned failures = 1;
            check(!d::file_writer::open(
                      event_loop.value, file(dir.path / "shutdown-retry"), {}, writer,
                      [&](auto op, auto value) {
                          if (op == d::writer_operation::queue_shutdown && failures) {
                              --failures;
                              return r::int64_t{-ENOMEM};
                          }
                          return value;
                      }),
                  "shutdown retry open");
            bool closed = false;
            check(!writer.close([&] { closed = true; }), "shutdown retry close");
            event_loop.run();
            check(closed && writer.closed() && failures == 0,
                  "failed cleanup submission retries asynchronously");
        }
        {
            loop event_loop;
            d::file_writer writer;
            check(
                !d::file_writer::open(event_loop.value, file(dir.path / "validation"), {}, writer),
                "validation open");
            check(writer.submit({}, 0, {}) == r::errc::invalid &&
                      writer.submit(buffers(), r::numeric_limits<r::uint64_t>::max(), {}) ==
                          r::errc::too_big &&
                      writer.submit(buffers(),
                                    static_cast<r::uint64_t>(r::numeric_limits<r::int64_t>::max()) -
                                        4096,
                                    {}) == r::errc::too_big,
                  "buffer, offset, and cumulative write-size validation");
            bool completed = false;
            unsigned closed = 0;
            check(!writer.submit(buffers(), 0, [&](auto status) { completed = !status; }),
                  "busy first request");
            check(writer.submit(buffers(), 0, {}) == r::errc::busy, "single writer backpressure");
            check(!writer.close([&] { ++closed; }) &&
                      writer.close([&] { closed += 10; }) == r::errc::busy,
                  "close preserves registered callback");
            check(writer.submit(buffers(), 0, {}) == r::errc::shutdown,
                  "closed writer rejects submission");
            event_loop.run();
            check(completed && closed == 1, "close callback exactly once");
        }
        {
            loop event_loop;
            d::file_writer writer;
            check(!d::file_writer::open(event_loop.value, file(dir.path / "throwing-callback"), {},
                                        writer),
                  "callback exception open");
            check(!writer.submit(buffers(), 0,
                                 [](auto) { throw std::runtime_error("completion failed"); }),
                  "callback exception submit");
            event_loop.run();
            check(writer.closed() && writer.last_error() == r::errc::io_error,
                  "callback exception is contained and closes writer");
        }
        if (!async_available(dir))
            return;
        for (unsigned mode = 0; mode < 4; ++mode) {
            loop event_loop;
            d::file_writer writer;
            check(!d::file_writer::open(
                      event_loop.value, file(dir.path / "poll-failure"), {true, true, 1}, writer,
                      [=](auto operation, auto value) {
                          if (operation == d::writer_operation::read_event && mode == 0)
                              return r::int64_t{-EIO};
                          if (operation == d::writer_operation::poll_result)
                              return mode == 1   ? r::int64_t{8}
                                     : mode == 2 ? r::int64_t{-EIO}
                                     : mode == 3 ? r::int64_t{-EAGAIN}
                                                 : value;
                          if (operation == d::writer_operation::queue_work && mode == 3)
                              return r::int64_t{-EIO};
                          return value;
                      }),
                  "polled failure open");
            unsigned completed = 0;
            bool closed = false;
            check(!writer.submit(buffers(), 0,
                                 [&](auto status) {
                                     ++completed;
                                     check(status ==
                                               (mode == 1 ? r::errc::no_space : r::errc::io_error),
                                           "polled completion failure mapping");
                                     check(!writer.close([&] { closed = true; }),
                                           "close after polled failure");
                                 }),
                  "polled failure submit");
            event_loop.run();
            check(completed == 1 && closed, "polled failure completes and safely drains");
        }
    }
    void allocations() {
        directory dir;
        unsigned failed = 0, succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 20; ++position) {
            const auto before = descriptors();
            {
                loop event_loop;
                d::file_writer writer;
                auto opened = file(dir.path / "allocation-init");
                allocation_failure::after(position);
                const auto status =
                    d::file_writer::open(event_loop.value, std::move(opened), {}, writer);
                allocation_failure::after(-1);
                if (status) {
                    ++failed;
                    check(status == r::errc::out_of_memory && writer.closed(),
                          "initialization allocation failure");
                } else {
                    ++succeeded;
                    check(!writer.close(), "allocation init close");
                }
                event_loop.run();
            }
            check(descriptors() == before, "allocation init releases descriptors");
        }
        check(failed && succeeded, "initialization allocation sweep");
        failed = 0;
        succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 25; ++position) {
            loop event_loop;
            d::file_writer writer;
            auto input = buffers();
            unsigned completed = 0;
            check(!d::file_writer::open(event_loop.value, file(dir.path / "allocation-submit"), {},
                                        writer),
                  "allocation submit open");
            r::completion callback = [&](auto status) {
                check(!status, "allocation submit result");
                ++completed;
            };
            allocation_failure::after(position);
            const auto status = writer.submit(input, 0, std::move(callback));
            allocation_failure::after(-1);
            if (status) {
                ++failed;
                check(status == r::errc::out_of_memory, "submit allocation failure");
            } else
                ++succeeded;
            r::close_callback closed = [] {};
            allocation_failure::after(0);
            const auto close_status = writer.close(std::move(closed));
            allocation_failure::after(-1);
            check(!close_status, "close requires no standard allocation");
            event_loop.run();
            check(completed == (status ? 0U : 1U), "allocation sweep exact completion contract");
        }
        check(failed && succeeded, "submit allocation sweep");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected writer suite");
        const r::string_view suite{argv[1]};
        // libuv lazily creates a process-wide signal pipe. Establish its
        // baseline independently of the writer before checking descriptor leaks.
        {
            loop runtime;
            uv_work_t request{};
            check(uv_queue_work(
                      &runtime.value, &request, [](uv_work_t*) {}, [](uv_work_t*, int) {}) == 0,
                  "initialize libuv runtime");
            runtime.run();
        }
        if (suite == "writer_basic")
            basic();
        else if (suite == "writer_tracing")
            tracing();
        else if (suite == "writer_async")
            asynchronous();
        else if (suite == "writer_close")
            closing();
        else if (suite == "writer_failure")
            failures();
        else if (suite == "writer_allocation")
            allocations();
        else if (suite == "writer_diagnostics")
            diagnostics();
        else
            throw std::runtime_error("unknown writer suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
