#ifndef RAFTPP_BENCHMARK_PROFILER_DATA_HPP
#define RAFTPP_BENCHMARK_PROFILER_DATA_HPP

// C++ port of cowsql/raft tools/benchmark/profiler.c; see LICENSE.md.
#include "device.hpp"
#include "report.hpp"

#ifndef RAFTPP_BENCHMARK_HAS_PERF
#define RAFTPP_BENCHMARK_HAS_PERF 0
#endif
namespace raftpp::benchmark {
    struct trace_field {
        size_t offset = 0, size = 0;
        bool dynamic = false;
    };
    class trace_format {
    public:
        static trace_format parse(string_view text);
        uint64_t number(view<const byte> raw, string_view name) const;
        view<const byte> bytes(view<const byte> raw, string_view name) const;
        string text(view<const byte> raw, string_view name) const;
        unsigned id() const noexcept {
            return id_;
        }

    private:
        unsigned id_ = 0;
        map<string, trace_field, std::less<>> fields_;
    };
    enum class kernel_event {
        block_queue,
        block_request_complete,
        block_bio_complete,
        nvme_setup,
        nvme_complete
    };
    struct event_format {
        kernel_event kind;
        trace_format format;
    };
    struct perf_sample {
        uint64_t time = 0;
        unsigned cpu = 0;
        vector<byte> raw;
    };
    vector<perf_sample> read_perf_ring(view<byte> mapping, size_t page_size);
    vector<int> parse_cpu_list(string_view text);
    struct kernel_measurements {
        latency_samples block, nvme;
    };
    kernel_measurements correlate(vector<perf_sample> samples, view<const event_format> formats,
                                  const device_info& device, uint64_t expected, unsigned issuer);
}
#endif // RAFTPP_BENCHMARK_PROFILER_DATA_HPP
