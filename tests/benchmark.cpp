#include "../tools/benchmark/submit.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <locale>

namespace r = raftpp;
namespace b = raftpp::benchmark;
namespace {
    void check(bool value, r::string_view message) {
        if (!value)
            throw std::runtime_error(r::string{message});
    }
    template <typename F> void rejected(F action) {
        bool failed = false;
        try {
            action();
        } catch (const r::invalid_argument&) {
            failed = true;
        }
        check(failed, "invalid measurement/input rejected");
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-benchmark-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "create benchmark test directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    void report() {
        b::latency_samples samples;
        rejected([&] { (void)samples.summarize(); });
        for (auto value : {4U, 1U, 8U, 3U, 2U})
            samples.add(value);
        auto result = samples.summarize();
        check(result.value == 3 && result.lower_bound == 1 && result.upper_bound == 8 &&
                  samples.size() == 5,
              "exact nearest-rank p50/min/max");
        b::latency_samples even;
        even.add(0);
        even.add(1000000000);
        result = even.summarize();
        check(result.value == 0 && result.upper_bound == 1000000000,
              "even/single-bucket-boundary samples remain exact");
        b::latency_samples single;
        single.add(30000000);
        result = single.summarize();
        check(result.value == 30000000 && result.lower_bound == result.upper_bound,
              "slow and singleton samples are not histogram-clipped");
        const auto rate = b::throughput(0.5, 1000000000);
        check(rate.value == 0.5 && rate.lower_bound == 0 && rate.upper_bound == 0,
              "fractional operation rates use 1e9 ns per second");
        rejected([&] { (void)b::throughput(1, 0); });
        rejected([&] { (void)b::throughput(1, r::numeric_limits<r::uint64_t>::max()); });
        b::report output;
        output.add("submit:2048", b::metric_kind::latency, result);
        output.add("rate", b::metric_kind::throughput, rate);
        rejected([&] { output.add("rate", b::metric_kind::throughput, rate); });
        rejected([&] { output.add("invalid\"name", b::metric_kind::latency, result); });
        rejected([&] { output.add("invalid", static_cast<b::metric_kind>(99), result); });
        rejected([&] {
            output.add("invalid", b::metric_kind::latency,
                       {r::numeric_limits<double>::infinity(), 0, 0});
        });
        struct comma_decimal : std::numpunct<char> {
            char do_decimal_point() const override {
                return ',';
            }
        };
        const auto previous = std::locale();
        std::locale::global(std::locale{previous, new comma_decimal});
        const auto json = output.json();
        std::locale::global(previous);
        check(json.find("\"value\": 0.5") != r::string::npos &&
                  json.find("\"lower_bound\"") != r::string::npos &&
                  json.find("\"upper_bound\"") != r::string::npos,
              "JSON metric numbers remain locale independent");
    }
    void options() {
        const auto defaults = b::parse_submit({});
        check(defaults.directory == "." && defaults.buffer == 2048 && defaults.size == 8388608 &&
                  defaults.operations() == 4096,
              "C submit defaults");
        const r::array<r::string_view, 7> arguments{
            "-d", "directory with spaces", "-b4K", "--size=8K", "--timeout", "1234", "--help"};
        const auto parsed = b::parse_submit(arguments);
        check(parsed.help && parsed.directory == "directory with spaces" && parsed.buffer == 4096 &&
                  parsed.size == 8192 && parsed.timeout == 1234,
              "separate/attached/equals options preserve string ownership");
        for (auto value : {"", "-1", "1x", "18446744073709551616", "18446744073709551615G"})
            rejected([&] { (void)b::bytes(value); });
        for (auto value : {"0", "8", "40", "49", "1048584"}) {
            const r::array<r::string_view, 2> args{"--buf", value};
            rejected([&] { (void)b::parse_submit(args); });
        }
        for (auto value : {"0", "1", "4097", "4294967296"}) {
            const r::array<r::string_view, 2> args{"--size", value};
            rejected([&] { (void)b::parse_submit(args); });
        }
        const r::array<r::string_view, 4> fractional{"--buf", "3072", "--size", "4096"};
        rejected([&] { (void)b::parse_submit(fractional); });
        const r::array<r::string_view, 1> missing{"--size"};
        rejected([&] { (void)b::parse_submit(missing); });
        const r::array<r::string_view, 2> unknown{"--unknown", "1"};
        rejected([&] { (void)b::parse_submit(unknown); });
        const r::array<r::string_view, 2> timeout{"--timeout", "0"};
        rejected([&] { (void)b::parse_submit(timeout); });
        auto invalid = defaults;
        invalid.buffer = 0;
        rejected([&] { (void)b::submit(invalid); });
    }
    void submit() {
        directory root;
        {
            std::ofstream marker{root.path / "preserved"};
            marker << "unrelated data";
        }
        auto intact = [&] {
            r::vector<r::fs::path> paths;
            for (const auto& path : r::fs::directory_iterator{root.path})
                paths.push_back(path.path().filename());
            check(paths.size() == 1 && paths.front() == "preserved",
                  "native benchmark cleans only its own temporary data after drain");
        };
        for (auto buffer : {4096U, 2048U, 64U}) {
            b::options settings;
            settings.directory = root.path;
            settings.buffer = buffer;
            settings.size = buffer == 2048 ? 8192 : 4096;
            const auto json = b::submit(settings).json();
            check(json.find("\"submit:" + std::to_string(buffer) + "\"") != r::string::npos &&
                      json.find("\"submit:write:" + std::to_string(buffer) + "\"") !=
                          r::string::npos,
                  "real singleton/warmed/small-entry runs measure commit and native write latency");
            intact();
        }
        b::options broken;
        broken.directory = root.path;
        broken.address = "127.0.0.1:invalid-service";
        bool failed = false;
        try {
            (void)b::submit(broken);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "native startup failure produces no successful report");
        intact();
        broken.directory = root.path / "preserved";
        failed = false;
        try {
            (void)b::submit(broken);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "regular-file --dir rejected without mutation");
        intact();
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "benchmark suite argument");
        const r::string_view suite{argv[1]};
        if (suite == "report")
            report();
        else if (suite == "options")
            options();
        else if (suite == "submit")
            submit();
        else
            check(false, "unknown benchmark suite");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
