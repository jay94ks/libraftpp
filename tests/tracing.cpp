#include <raftpp/tracing.hpp>
#include "tracing.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace {
    void check(bool value, r::string_view text) {
        if (!value)
            throw std::runtime_error(r::string{text});
    }
    struct record {
        int level = 0;
        r::string text, file;
        unsigned line = 0;
    };
    void callback() {
        r::vector<record> cpp;
        r::tracer tracing{[&](const auto& event) {
            check(event.type == r::trace_diagnostic, "diagnostic type");
            const auto& value = *static_cast<const r::diagnostic_trace*>(event.info);
            cpp.push_back({static_cast<int>(value.level), r::string{value.message},
                           value.location.file_name(), value.location.line()});
        }};
        for (r::size_t size : {0U, 1U, 100U, 1010U, 1023U, 1024U, 2048U}) {
            const r::string text(size, 'x');
            for (int level = 1; level <= 5; ++level) {
                const auto location = r::source_location::current();
                r::detail::logf(tracing, static_cast<r::trace_level>(level), location, "%s %llu",
                                text.c_str(), 123456789ULL);
                check(cpp.back().text == (text + " 123456789").substr(0, 1023) &&
                          cpp.back().level == level,
                      "bounded format and level contract");
                check(cpp.back().file == __FILE__ && cpp.back().line == location.line(),
                      "source location belongs to caller");
                check(cpp.back().text.size() <= 1023, "C-compatible format bound");
            }
        }
        const auto line = r::source_location::current().line() + 1;
        tracing.log(r::trace_level::debug, "direct");
        check(cpp.back().line == line && cpp.back().text == "direct",
              "default source location captures call site");
        unsigned custom = 0;
        const int data = 7;
        tracing = r::tracer{[&](const auto& event) {
            check(event.type == 65536 && event.info == &data, "custom opaque event borrow");
            ++custom;
        }};
        tracing.emit(65536, &data);
        check(custom == 1, "application event emitted");
        auto empty = r::tracer{r::trace_callback{}};
        check(!empty.enabled(), "empty callable disables tracer");
    }
    void lifetime() {
        r::tracer tracing;
        auto token = std::make_shared<int>(7);
        r::weak<int> observer = token;
        unsigned calls = 0;
        tracing = r::tracer{[&, token](const auto&) {
            ++calls;
            tracing = r::tracer::noop();
            check(!observer.expired() && *token == 7, "active callback survives replacement");
        }};
        token.reset();
        tracing.log(r::trace_level::info, "replace");
        check(calls == 1 && observer.expired(), "custom callable releases after return");
        tracing = r::tracer{[&](const auto&) {
            ++calls;
            tracing.log(r::trace_level::info, "recursive");
        }};
        tracing.log(r::trace_level::info, "outer");
        check(calls == 2, "recursive tracing suppressed on current thread");
        tracing = r::tracer{[&](const auto&) {
            ++calls;
            throw r::bad_alloc{};
        }};
        tracing.log(r::trace_level::error, "throwing");
        tracing.log(r::trace_level::error, "throwing again");
        check(calls == 4, "exceptions suppressed and recursion state restored");
        r::trace_callback fixed = [&](const auto&) { ++calls; };
        tracing = r::tracer{fixed};
        allocation_failure::after(0);
        auto copy = tracing;
        copy.log(r::trace_level::info, "allocation free");
        r::detail::logf(copy, r::trace_level::trace, r::source_location::current(), "%s %d",
                        "fixed", 42);
        r::tracer::noop().log(r::trace_level::info, "noop");
        allocation_failure::after(-1);
        check(calls == 6, "emit/copy/format/noop require no allocation");
        bool failed = false;
        allocation_failure::after(0);
        try {
            auto value = r::tracer{fixed};
            (void)value;
        } catch (const r::bad_alloc&) {
            failed = true;
        }
        allocation_failure::after(-1);
        check(failed, "callable storage allocation failure propagates from constructor");
    }
    struct environment {
        r::optional<r::string> saved;
        environment() {
            if (auto value = std::getenv("LIBRAFT_TRACE"))
                saved = value;
        }
        ~environment() {
            setenv("LIBRAFT_TRACE", "", 1);
            r::maybe_enable_tracing(false);
            if (saved)
                setenv("LIBRAFT_TRACE", saved->c_str(), 1);
            else
                unsetenv("LIBRAFT_TRACE");
        }
    };
    struct capture {
        int original;
        FILE* stream;
        capture() : original(dup(STDERR_FILENO)), stream(std::tmpfile()) {
            check(original >= 0 && stream, "stderr capture resources");
            std::fflush(stderr);
            check(dup2(fileno(stream), STDERR_FILENO) >= 0, "redirect stderr");
        }
        ~capture() {
            std::fflush(stderr);
            dup2(original, STDERR_FILENO);
            close(original);
            std::fclose(stream);
        }
        r::string read() {
            std::fflush(stderr);
            check(!std::fseek(stream, 0, SEEK_END), "synchronize capture position");
            const auto size = std::ftell(stream);
            check(size >= 0, "capture size");
            std::rewind(stream);
            r::string output(static_cast<r::size_t>(size), '\0');
            check(std::fread(output.data(), 1, output.size(), stream) == output.size(),
                  "read capture");
            std::fseek(stream, 0, SEEK_END);
            return output;
        }
    };
    void stderr_output() {
        environment restore;
        setenv("LIBRAFT_TRACE", "", 1);
        r::maybe_enable_tracing(false);
        unsetenv("LIBRAFT_TRACE");
        r::tracer tracing;
        capture output;
        r::maybe_enable_tracing(true);
        tracing.log(r::trace_level::error, "disabled");
        check(output.read().empty() && !r::stderr_tracing_enabled(),
              "unset environment does not enable default tracer");
        setenv("LIBRAFT_TRACE", "", 1);
        r::maybe_enable_tracing(true);
        tracing.emit(r::trace_write_submit);
        tracing.emit(65536);
        tracing.emit(r::trace_diagnostic);
        check(output.read().empty(), "stderr ignores metrics/custom/null diagnostic");
        const auto location = r::source_location::current();
        tracing.log(r::trace_level::info, "C++ diagnostic", location);
        auto text = output.read();
        check(text.starts_with("LIBRAFT   ") && std::count(text.begin(), text.end(), '\n') == 1,
              "stderr prefix and diagnostic line: " + text);
        check(text.find(r::string{location.file_name()} + ":" + std::to_string(location.line()) +
                        " C++ diagnostic\n") != r::string::npos,
              "stderr source location and message match the diagnostic contract");
        unsetenv("LIBRAFT_TRACE");
        r::maybe_enable_tracing(false);
        check(r::stderr_tracing_enabled(), "unset environment leaves existing switch unchanged");
        setenv("LIBRAFT_TRACE", "0", 1);
        r::maybe_enable_tracing(false);
        tracing.log(r::trace_level::error, "disabled again");
        check(output.read() == text && !r::stderr_tracing_enabled(),
              "environment presence permits disabling regardless of its value");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite argument");
        r::string_view suite = argv[1];
        if (suite == "tracing_callback")
            callback();
        else if (suite == "tracing_lifetime")
            lifetime();
        else if (suite == "tracing_stderr")
            stderr_output();
        else
            check(false, "unknown suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
