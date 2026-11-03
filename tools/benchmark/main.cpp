// C++ port of cowsql/raft tools/benchmark/main.c; see LICENSE.md.
#include "submit.hpp"
#include "disk.hpp"
#include <csignal>
#include <iostream>

namespace r = raftpp;
namespace b = raftpp::benchmark;
namespace {
    void usage() {
        std::cout
            << "usage: raftpp-benchmark {submit|disk} [options]\n"
            << "  --dir, -d PATH    Existing directory for temporary files (default .)\n"
            << "                   disk also accepts a block device; overwrites its first size bytes\n"
            << "  --buf, -b BYTES   submit: encoded entry 48..1M, multiple of 8 (default 2048)\n"
            << "                   disk: power of two 512..1M (default 4096)\n"
            << "  --size, -s BYTES  Positive bytes <= UINT32_MAX, divisible by buf (default 8M)\n"
            << "                   submit additionally requires a multiple of 4096\n"
            << "  --address ADDR    Single-voter TCP bind/advertised address (default 127.0.0.1:0)\n"
            << "  --timeout MS      Execution deadline (default 30000)\n"
            << "  --perf, -p        disk: report kernel block/NVMe latency instead of end-to-end metrics\n"
            << "  --trace, -t NAMES disk: comma-separated tracefs subsystems; restores enable states\n"
            << "  --tracefs PATH   disk: existing tracefs mount (default /sys/kernel/tracing)\n"
            << "  --help            Show usage; K/M/G sizes use powers of 1024\n";
    }
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && r::string_view{argv[1]} == "--help") {
            usage();
            return 0;
        }
        if (argc < 2) {
            usage();
            return 1;
        }
        const r::string_view command{argv[1]};
        r::vector<r::string_view> arguments;
        for (int i = 2; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        std::signal(SIGPIPE, SIG_IGN);
        b::report result;
        if (command == "submit") {
            const auto settings = b::parse_submit(arguments);
            if (settings.help) {
                usage();
                return 0;
            }
            result = b::submit(settings);
        } else if (command == "disk") {
            const auto settings = b::parse_disk(arguments);
            if (settings.help) {
                usage();
                return 0;
            }
            result = b::disk(settings);
        } else
            throw r::invalid_argument("unknown benchmark: " + r::string{command});
        const auto output = result.json();
        std::cout << output;
        std::cout.flush();
        if (!std::cout)
            throw std::runtime_error("write benchmark report failed");
    } catch (const std::exception& error) {
        std::cerr << "benchmark: " << error.what() << '\n';
        return 1;
    }
}
