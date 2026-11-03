#ifndef RAFTPP_BENCHMARK_PROFILER_HPP
#define RAFTPP_BENCHMARK_PROFILER_HPP

#include "profiler_data.hpp"
#include <sys/types.h>
#if RAFTPP_BENCHMARK_HAS_PERF
#include <linux/perf_event.h>
#endif

namespace raftpp::benchmark {
    bool valid_trace_system(string_view name) noexcept;
    class trace_session {
    public:
        trace_session(const fs::path& root, view<const string> systems);
        ~trace_session();
        trace_session(const trace_session&) = delete;
        trace_session& operator=(const trace_session&) = delete;
        void start();
        error_code stop() noexcept;

    private:
        struct toggle {
            fs::path path;
            char previous = '0';
            bool changed = false;
        };
        vector<toggle> toggles_;
        bool started_ = false;
    };
#if RAFTPP_BENCHMARK_HAS_PERF
    struct perf_operations {
        virtual ~perf_operations() = default;
        virtual int open(perf_event_attr* attributes, int cpu, int group) noexcept;
        virtual int control(int fd, unsigned long request, unsigned long argument) noexcept;
        virtual void* map(size_t size, int fd) noexcept;
        virtual int unmap(void* address, size_t size) noexcept;
        virtual int close(int fd) noexcept;
        virtual ssize_t read(int fd, void* data, size_t size) noexcept;
        virtual uint64_t switches();
    };
    perf_operations& native_perf_operations() noexcept;
    class perf_group {
    public:
        perf_group(int cpu, view<const event_format> formats, size_t page_size,
                   ref<perf_operations> operations);
        ~perf_group();
        perf_group(const perf_group&) = delete;
        perf_group& operator=(const perf_group&) = delete;
        void start();
        void stop();
        error_code close() noexcept;
        vector<perf_sample> poll();

    private:
        ref<perf_operations> operations_;
        vector<int> descriptors_;
        void* mapping_ = nullptr;
        size_t page_size_ = 0, mapping_size_ = 0;
        bool enabled_ = false;
    };
#endif
    class kernel_profiler {
    public:
        kernel_profiler(const device_info& device, bool perf, view<const string> traces,
                        const fs::path& tracefs = "/sys/kernel/tracing"
#if RAFTPP_BENCHMARK_HAS_PERF
                        ,
                        ref<perf_operations> operations = native_perf_operations(),
                        vector<int> cpus = {}
#endif
        );
        void start();
        void poll();
        void stop();
        void report_into(report& output, size_t buffer, uint64_t expected, unsigned issuer);

    private:
        device_info device_;
        bool perf_ = false, started_ = false, stopped_ = false;
        uint64_t switches_ = 0;
        optional<trace_session> trace_;
        vector<event_format> formats_;
        vector<perf_sample> samples_;
#if RAFTPP_BENCHMARK_HAS_PERF
        ref<perf_operations> operations_;
        vector<owned<perf_group>> groups_;
#endif
    };
}
#endif // RAFTPP_BENCHMARK_PROFILER_HPP
