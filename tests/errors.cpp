#include <raftpp/error.hpp>
#include <raftpp.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#if RAFTPP_WITH_UV
#include "file.hpp"
#include "disk.hpp"
#include <fcntl.h>
#include <uv.h>
#include <unistd.h>
#endif

namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
    extern thread_local std::ptrdiff_t remaining;
}
namespace {
    void check(bool value, r::string_view message) {
        if (!value)
            throw std::runtime_error(r::string{message});
    }
    void messages() {
        constexpr r::array<r::string_view, 24> expected{
            "success",
            "out of memory",
            "server ID is not valid",
            "server ID already in use",
            "server address already in use",
            "server role is not valid",
            "encoded data is malformed",
            "server is not the leader",
            "server has lost leadership",
            "server is shutting down",
            "bootstrap only works on new clusters",
            "a configuration change is already in progress",
            "persisted data is corrupted",
            "operation canceled",
            "resource name too long",
            "data is too big",
            "no connection to remote server available",
            "operation can't be performed at this time",
            "I/O error",
            "Resource not found",
            "Invalid parameter",
            "No access to resource",
            "Not enough disk space",
            "System or raft limit met or exceeded"};
        for (r::size_t i = 0; i < expected.size(); ++i) {
            const auto code = static_cast<r::errc>(i);
            check(r::error_message(code) == expected[i] &&
                      r::make_error_code(code).message() == expected[i],
                  "stable numeric error mapping and messages");
        }
        check(r::error_message(static_cast<r::errc>(99)) == "unknown error",
              "unknown error message");
    }
    void context() {
        for (r::size_t length : {0U, 1U, 252U, 253U, 254U, 255U, 256U, 1000U}) {
            const r::string prefix(length, 'p');
            auto info = r::make_error_info(r::errc::io_error);
            auto expected = prefix.size() < 253 ? prefix + ": I/O error" : prefix;
            expected.resize(std::min<r::size_t>(expected.size(), 255));
            info = r::wrap_error_info(info, prefix);
            check(info.text() == expected && info.message.back() == '\0',
                  "bounded wrapping matches fixed boundary contract");
        }
        const auto cause = std::make_error_code(std::errc::no_space_on_device);
        allocation_failure::after(0);
        auto info = r::make_error_info(r::errc::io_error, "append", "store/segment", cause);
        auto copy = r::wrap_error_info(info, "node");
        const bool no_allocation = allocation_failure::remaining == 0;
        allocation_failure::after(-1);
        check(no_allocation && copy.code == r::errc::io_error && copy.cause == cause &&
                  copy.text().starts_with("node: append: store/segment: I/O error (generic:"),
              "context and copies allocate no storage");
        info = {};
        check(copy.code && copy.cause == cause, "copy survives original reset");
        check(!r::make_error_info({}, "unused", "unused", cause).code,
              "success produces empty diagnostics");
        auto foreign =
            r::make_error_info(std::make_error_code(std::errc::permission_denied), "external");
        check(foreign.text().starts_with("external: generic:"),
              "foreign categories retain category and value");
        std::ranges::fill(copy.message, 'x');
        check(copy.text().size() == copy.message.size(),
              "malformed public storage is read within its bound");
    }
#if RAFTPP_WITH_UV
    struct network : r::transport {
        r::error_code init(r::server_id, r::string_view) override {
            return {};
        }
        r::error_code start(r::function<void(r::message)>) override {
            return {};
        }
        r::error_code send(r::shared<const r::message>, r::completion) override {
            return r::errc::no_connection;
        }
        void close(r::close_callback callback) noexcept override {
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
    void native() {
        r::string pattern = "/tmp/raftpp-error-XXXXXX";
        check(mkdtemp(pattern.data()) != nullptr, "temporary directory");
        struct cleanup {
            r::fs::path path;
            ~cleanup() {
                r::error_code ignored;
                r::fs::remove_all(path, ignored);
            }
        } dir{pattern};
        const auto missing = dir.path / "missing";
        r::error_info info;
        r::detail::file_handle file;
        check(r::detail::file_handle::open(missing, O_RDWR, 0600, file, &info) ==
                      r::errc::io_error &&
                  info.cause == std::errc::no_such_file_or_directory &&
                  info.text().find(missing.native()) != r::string_view::npos,
              "file open captures native cause and owned path");
        const auto path = dir.path / "file";
        check(!r::detail::file_handle::open(path, O_RDWR | O_CREAT, 0600, file, &info) &&
                  !info.code && !file.allocate(4096, &info) && !info.code && !file.close(&info),
              "successful native calls clear supplied diagnostics");
        check(!r::detail::file_handle::open(path, O_RDONLY, 0, file), "read-only file open");
        check(file.allocate(4096, &info) == r::errc::io_error &&
                  info.cause == std::errc::bad_file_descriptor,
              "allocation captures POSIX error");
        check(!file.close(), "file close");
        r::detail::disk_storage disk{missing};
        check(disk.open(&info) == r::errc::not_found &&
                  info.cause == std::errc::no_such_file_or_directory &&
                  info.text().starts_with("disk.stat:"),
              "directory initialization captures native cause");
        uv_loop_t loop{};
        check(!uv_loop_init(&loop), "loop initialization");
        {
            auto backend = std::make_shared<r::uv_io>(loop, missing, std::make_shared<network>());
            check(backend->init(1, "one") == r::errc::not_found &&
                      backend->error_details().cause == std::errc::no_such_file_or_directory,
                  "native initialization diagnostic crosses virtual I/O interface");
            info = backend->error_details();
            backend.reset();
            bool threw = false;
            try {
                auto failed =
                    std::make_shared<r::uv_io>(loop, missing, std::make_shared<network>());
                r::node node{1, "one", failed, std::make_shared<machine>()};
            } catch (const r::system_error& error) {
                threw = error.code() == r::errc::not_found &&
                        r::string_view{error.what()}.find(missing.native()) != r::string_view::npos;
            }
            check(threw && info.cause == std::errc::no_such_file_or_directory,
                  "node constructor preserves owned native context");
        }
        uv_run(&loop, UV_RUN_DEFAULT);
        check(!uv_loop_close(&loop), "failed initialization leaves no handles");
    }
#endif
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected error suite");
        const r::string_view suite{argv[1]};
        if (suite == "error_messages")
            messages();
        else if (suite == "error_context")
            context();
#if RAFTPP_WITH_UV
        else if (suite == "native_errors")
            native();
#endif
        else
            throw std::runtime_error("unknown error suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
