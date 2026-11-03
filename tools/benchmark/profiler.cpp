// C++ port of cowsql/raft tools/benchmark/profiler.c; see LICENSE.md.
#include "profiler.hpp"
#include "file.hpp"
#include <bit>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace raftpp::benchmark {
    namespace {
        string load(const fs::path& path) {
            std::ifstream input{path};
            if (!input)
                throw system_error(errno ? errno : EIO, std::generic_category(),
                                   "read profiler metadata " + path.string());
            std::ostringstream text;
            text << input.rdbuf();
            if (input.bad())
                throw std::runtime_error("read profiler metadata failed " + path.string());
            return text.str();
        }
        error_code set_toggle(const fs::path& path, char value) noexcept {
            detail::file_handle file;
            if (auto error =
                    detail::file_handle::open(path, O_WRONLY | O_CLOEXEC | O_TRUNC, 0, file))
                return error;
            const char data[]{value, '\n'};
            ssize_t size;
            do {
                size = ::write(file.get(), data, sizeof data);
            } while (size < 0 && errno == EINTR);
            if (size < 0)
                return {errno, std::generic_category()};
            if (size != sizeof data)
                return std::make_error_code(std::errc::io_error);
            return file.close();
        }
#if RAFTPP_BENCHMARK_HAS_PERF
        [[noreturn]] void fail(string_view operation) {
            const auto cause = errno;
            throw system_error(cause, std::generic_category(), string{operation});
        }
#endif
    }
    bool valid_trace_system(string_view name) noexcept {
        if (name.empty())
            return false;
        for (const auto c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-'))
                return false;
        return true;
    }
    trace_session::trace_session(const fs::path& root, view<const string> systems) {
        auto save = [&](const fs::path& path) {
            const auto state = load(path);
            if (state != "0\n" && state != "1\n" && state != "0" && state != "1")
                throw invalid_argument("invalid tracefs enable state " + path.string());
            toggles_.push_back({path, state.front(), false});
        };
        for (const auto& system : systems) {
            if (!valid_trace_system(system))
                throw invalid_argument("invalid trace subsystem name");
            const auto begin = toggles_.size();
            for (const auto& entry : fs::directory_iterator{root / "events" / system}) {
                if (entry.is_directory() && fs::exists(entry.path() / "enable"))
                    save(entry.path() / "enable");
            }
            if (toggles_.size() == begin)
                throw invalid_argument("trace subsystem has no events: " + system);
        }
        if (!systems.empty())
            save(root / "tracing_on"); // Last enabled, first restored.
    }
    void trace_session::start() {
        if (started_)
            throw std::logic_error("trace session already started");
        started_ = true;
        try {
            for (auto& toggle : toggles_) {
                if (toggle.previous == '1')
                    continue;
                toggle.changed = true; // A partial failed write must also be restored.
                if (auto error = set_toggle(toggle.path, '1'))
                    throw system_error(error, "enable tracing " + toggle.path.string());
            }
        } catch (...) {
            (void)stop();
            throw;
        }
    }
    error_code trace_session::stop() noexcept {
        error_code first;
        for (auto i = toggles_.rbegin(); i != toggles_.rend(); ++i) {
            if (!i->changed)
                continue;
            if (auto error = set_toggle(i->path, i->previous)) {
                if (!first)
                    first = error;
            } else
                i->changed = false;
        }
        return first;
    }
    trace_session::~trace_session() {
        (void)stop();
    }
#if RAFTPP_BENCHMARK_HAS_PERF
    int perf_operations::open(perf_event_attr* attributes, int cpu, int group) noexcept {
        return static_cast<int>(
            syscall(SYS_perf_event_open, attributes, -1, cpu, group, PERF_FLAG_FD_CLOEXEC));
    }
    int perf_operations::control(int fd, unsigned long request, unsigned long argument) noexcept {
        return ioctl(fd, request, argument);
    }
    void* perf_operations::map(size_t size, int fd) noexcept {
        return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    int perf_operations::unmap(void* address, size_t size) noexcept {
        return munmap(address, size);
    }
    int perf_operations::close(int fd) noexcept {
        return ::close(fd);
    }
    ssize_t perf_operations::read(int fd, void* data, size_t size) noexcept {
        return ::read(fd, data, size);
    }
    uint64_t perf_operations::switches() {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) < 0)
            fail("getrusage profiler");
        return static_cast<uint64_t>(usage.ru_nvcsw);
    }
    perf_operations& native_perf_operations() noexcept {
        static perf_operations instance;
        return instance;
    }
    perf_group::perf_group(int cpu, view<const event_format> formats, size_t page_size,
                           ref<perf_operations> operations)
        : operations_(operations), page_size_(page_size) {
        if (formats.empty() || !std::has_single_bit(page_size) ||
            page_size < sizeof(perf_event_mmap_page) ||
            page_size > numeric_limits<size_t>::max() / 65)
            throw invalid_argument("invalid perf group configuration");
        mapping_size_ = page_size * 65;
        descriptors_.reserve(formats.size());
        auto& api = operations_.get();
        try {
            for (const auto& event : formats) {
                const bool leader = descriptors_.empty();
                perf_event_attr attributes{};
                attributes.type = PERF_TYPE_TRACEPOINT;
                attributes.size = sizeof attributes;
                attributes.config = event.format.id();
                attributes.sample_period = 1;
                attributes.sample_type = PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_RAW;
                attributes.disabled = leader;
                attributes.use_clockid = 1;
                attributes.clockid = CLOCK_MONOTONIC;
                attributes.read_format =
                    PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
                const int fd = api.open(&attributes, cpu, leader ? -1 : descriptors_.front());
                if (fd < 0)
                    fail("perf_event_open");
                descriptors_.push_back(fd); // Capacity was reserved before native ownership.
                if (leader) {
                    mapping_ = api.map(mapping_size_, fd);
                    if (mapping_ == MAP_FAILED) {
                        mapping_ = nullptr;
                        fail("mmap perf ring");
                    }
                } else if (api.control(fd, PERF_EVENT_IOC_SET_OUTPUT,
                                       static_cast<unsigned long>(descriptors_.front())) < 0)
                    fail("perf redirect sample output");
            }
        } catch (...) {
            (void)close();
            throw;
        }
    }
    void perf_group::start() {
        if (enabled_)
            throw std::logic_error("perf group already enabled");
        auto& api = operations_.get();
        if (api.control(descriptors_.front(), PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0)
            fail("reset perf group");
        enabled_ = true;
        if (api.control(descriptors_.front(), PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0)
            fail("enable perf group");
    }
    void perf_group::stop() {
        if (!enabled_)
            return;
        auto& api = operations_.get();
        if (api.control(descriptors_.front(), PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0)
            fail("disable perf group");
        enabled_ = false;
        array<uint64_t, 3> counts{};
        ssize_t size;
        do {
            size = api.read(descriptors_.front(), counts.data(), sizeof counts);
        } while (size < 0 && errno == EINTR);
        if (size < 0)
            fail("read perf scheduling counters");
        if (size != sizeof counts || counts[1] != counts[2])
            throw std::runtime_error("incomplete or multiplexed perf collection");
    }
    vector<perf_sample> perf_group::poll() {
        if (!mapping_)
            throw std::logic_error("perf group is closed");
        return read_perf_ring({static_cast<byte*>(mapping_), mapping_size_}, page_size_);
    }
    error_code perf_group::close() noexcept {
        auto& api = operations_.get();
        error_code error;
        auto retain = [&](int status) {
            if (status < 0 && !error)
                error = {errno, std::generic_category()};
        };
        if (enabled_ && !descriptors_.empty())
            retain(api.control(descriptors_.front(), PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP));
        if (mapping_) {
            retain(api.unmap(mapping_, mapping_size_));
            mapping_ = nullptr;
        }
        for (auto i = descriptors_.rbegin(); i != descriptors_.rend(); ++i)
            retain(api.close(*i));
        descriptors_.clear();
        enabled_ = false;
        return error;
    }
    perf_group::~perf_group() {
        (void)close();
    }
#endif
    kernel_profiler::kernel_profiler(const device_info& device, bool perf,
                                     view<const string> traces, const fs::path& tracefs
#if RAFTPP_BENCHMARK_HAS_PERF
                                     ,
                                     ref<perf_operations> operations, vector<int> cpus
#endif
                                     )
        : device_(device), perf_(perf)
#if RAFTPP_BENCHMARK_HAS_PERF
          ,
          operations_(operations)
#endif
    {
        if (!traces.empty())
            trace_.emplace(tracefs, traces);
        if (!perf_)
            return;
#if RAFTPP_BENCHMARK_HAS_PERF
        if (device_.driver == device_driver::generic || device_.end_sector <= device_.first_sector)
            throw system_error(std::make_error_code(std::errc::operation_not_supported),
                               "kernel perf requires an identified NVMe or null-block device");
        auto add = [&](kernel_event kind, string_view name) {
            auto format = trace_format::parse(load(tracefs / "events" / name / "format"));
            if (std::any_of(formats_.begin(), formats_.end(),
                            [&](const auto& value) { return value.format.id() == format.id(); }))
                throw invalid_argument("duplicate kernel tracepoint ID");
            formats_.push_back({kind, std::move(format)});
        };
        add(kernel_event::block_queue, "block/block_bio_queue");
        add(kernel_event::block_request_complete, "block/block_rq_complete");
        add(kernel_event::block_bio_complete, "block/block_bio_complete");
        if (device_.driver == device_driver::nvme) {
            if (device_.disk_name.empty() || device_.logical_block_size < 512 ||
                device_.logical_block_size % 512)
                throw invalid_argument("missing NVMe namespace or logical block size");
            add(kernel_event::nvme_setup, "nvme/nvme_setup_cmd");
            add(kernel_event::nvme_complete, "nvme/nvme_complete_rq");
        }
        if (cpus.empty())
            cpus = parse_cpu_list(load("/sys/devices/system/cpu/online"));
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0)
            throw std::runtime_error("invalid system page size");
        groups_.reserve(cpus.size());
        for (const auto cpu : cpus)
            groups_.push_back(std::make_unique<perf_group>(
                cpu, formats_, static_cast<size_t>(page_size), operations_));
#else
        throw system_error(std::make_error_code(std::errc::operation_not_supported),
                           "perf headers unavailable at build time");
#endif
    }
    void kernel_profiler::start() {
        if (started_)
            throw std::logic_error("kernel profiler already started");
        started_ = true;
        if (trace_)
            trace_->start();
#if RAFTPP_BENCHMARK_HAS_PERF
        if (perf_) {
            for (auto& group : groups_)
                group->start();
            switches_ = operations_.get().switches();
        }
#endif
    }
    void kernel_profiler::poll() {
#if RAFTPP_BENCHMARK_HAS_PERF
        for (auto& group : groups_) {
            auto events = group->poll();
            for (auto& sample : events)
                samples_.push_back(std::move(sample));
        }
#endif
    }
    void kernel_profiler::stop() {
        if (!started_ || stopped_)
            throw std::logic_error("kernel profiler is not active");
#if RAFTPP_BENCHMARK_HAS_PERF
        if (perf_) {
            const auto now = operations_.get().switches();
            if (now < switches_)
                throw std::runtime_error("profiler context-switch counter went backwards");
            switches_ = now - switches_;
            for (auto& group : groups_)
                group->stop();
        }
#endif
        if (trace_) {
            if (const auto error = trace_->stop())
                throw system_error(error, "restore tracing state");
        }
        poll();
#if RAFTPP_BENCHMARK_HAS_PERF
        for (auto& group : groups_) {
            if (auto error = group->close())
                throw system_error(error, "close perf group");
        }
#endif
        stopped_ = true;
        if (perf_ && switches_)
            throw std::runtime_error(
                "unexpected voluntary context switches during kernel profiling");
    }
    void kernel_profiler::report_into(report& output, size_t buffer, uint64_t expected,
                                      unsigned issuer) {
        if (!perf_ || !stopped_)
            throw std::logic_error("kernel measurements are not complete");
        auto data = correlate(std::move(samples_), formats_, device_, expected, issuer);
        output.add("disk:block:" + std::to_string(buffer), metric_kind::latency,
                   data.block.summarize());
        if (device_.driver == device_driver::nvme)
            output.add("disk:nvme:" + std::to_string(buffer), metric_kind::latency,
                       data.nvme.summarize());
    }
}
