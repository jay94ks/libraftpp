#include "../tools/benchmark/profiler.hpp"
#include "../tools/benchmark/disk_options.hpp"
#include <raftpp/buffer.hpp>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <unistd.h>

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
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "malformed/incomplete profiler input rejected");
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-profiler-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "create profiler fixture");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    void write(const r::fs::path& path, r::string_view value) {
        r::fs::create_directories(path.parent_path());
        std::ofstream out{path};
        out << value;
        check(static_cast<bool>(out), "write profiler fixture");
    }
    r::string read(const r::fs::path& path) {
        std::ifstream in{path};
        r::string value;
        std::getline(in, value);
        return value;
    }
    r::string format(unsigned id, bool nvme = false) {
        r::string result = "name: synthetic\nID: " + std::to_string(id) + "\nformat:\n";
        auto field = [&](r::string_view type, r::string_view name, unsigned offset, unsigned size) {
            result += " field:" + r::string{type} + " " + r::string{name} +
                      "; offset:" + std::to_string(offset) + "; size:" + std::to_string(size) +
                      "; signed:0;\n";
        };
        field("unsigned short", "common_type", 0, 2);
        field("int", "common_pid", 4, 4);
        if (!nvme) {
            field("dev_t", "dev", 8, 4);
            field("sector_t", "sector", 16, 8);
            field("unsigned int", "nr_sector", 24, 4);
            field("int", "error", 28, 4);
            field("char", "rwbs[8]", 32, 8);
        } else {
            field("char", "disk[16]", 8, 16);
            field("int", "ctrl_id", 24, 4);
            field("int", "qid", 28, 4);
            field("int", "cid", 32, 4);
            field("u8", "opcode", 36, 1);
            field("u8", "cdw10[24]", 40, 24);
            field("u16", "status", 64, 2);
        }
        return result;
    }
    template <typename T> void put(r::vector<r::byte>& bytes, size_t offset, T value) {
        check(offset + sizeof value <= bytes.size(), "test raw buffer bounds");
        std::memcpy(bytes.data() + offset, &value, sizeof value);
    }
    b::perf_sample block(unsigned id, r::uint64_t time, r::uint64_t sector = 128,
                         bool partition = false) {
        b::perf_sample value{time, 0, r::vector<r::byte>(80)};
        put(value.raw, 0, static_cast<r::uint16_t>(id));
        put(value.raw, 4, 7U);
        put(value.raw, 8, static_cast<r::uint32_t>((8U << 20) | (partition ? 1U : 0U)));
        put(value.raw, 16, sector);
        put(value.raw, 24, 8U);
        value.raw[32] = r::byte{'W'};
        return value;
    }
    b::perf_sample nvme(unsigned id, r::uint64_t time, unsigned command = 5) {
        b::perf_sample value{time, 1, r::vector<r::byte>(80)};
        put(value.raw, 0, static_cast<r::uint16_t>(id));
        put(value.raw, 4, 7U);
        std::memcpy(value.raw.data() + 8, "nvme9n1", 8);
        put(value.raw, 24, 9U);
        put(value.raw, 28, 1U);
        put(value.raw, 32, command);
        value.raw[36] = r::byte{1};
        value.raw[40] = r::byte{16};
        value.raw[51] = r::byte{0x40};
        return value;
    }
    b::device_info device() {
        b::device_info value;
        value.driver = b::device_driver::nvme;
        value.device = makedev(8, 1);
        value.parent_device = makedev(8, 0);
        value.first_sector = 128;
        value.end_sector = 4096;
        value.disk_name = "nvme9n1";
        value.logical_block_size = 4096;
        return value;
    }
    r::vector<b::event_format> formats() {
        return {{b::kernel_event::block_queue, b::trace_format::parse(format(1))},
                {b::kernel_event::block_request_complete, b::trace_format::parse(format(2))},
                {b::kernel_event::block_bio_complete, b::trace_format::parse(format(3))},
                {b::kernel_event::nvme_setup, b::trace_format::parse(format(4, true))},
                {b::kernel_event::nvme_complete, b::trace_format::parse(format(5, true))}};
    }
    void options() {
        const r::array<r::string_view, 5> args{"-p", "-tblock,nvme", "--trace=sched,xhci-hcd",
                                               "--tracefs", "fixture path"};
        const auto value = b::parse_disk(args);
        check(value.perf &&
                  value.traces == r::vector<r::string>{"block", "nvme", "sched", "xhci-hcd"} &&
                  value.tracefs == "fixture path",
              "perf/trace options own strings and parse all subsystems");
        for (auto names : {"", ",block", "block,", "block,,nvme", "../block",
                           "block/block_bio_queue", "block,block"}) {
            const r::array<r::string_view, 2> arg{"--trace", names};
            rejected([&] { (void)b::parse_disk(arg); });
        }
        check(!b::valid_trace_system("../") && b::valid_trace_system("dma_fence") &&
                  b::valid_trace_system("xhci-hcd"),
              "trace subsystem path validation including hyphens");
    }
    void field_format() {
        const auto f = b::trace_format::parse(format(1));
        const auto value = block(1, 100);
        check(f.number(value.raw, "sector") == 128 && f.text(value.raw, "rwbs") == "W",
              "format-driven unaligned-safe scalar/array reads");
        rejected([&] { (void)f.number(r::view<const r::byte>{value.raw}.first(20), "sector"); });
        rejected([&] { (void)f.number(value.raw, "missing"); });
        auto dynamic = b::trace_format::parse(
            "ID: 6\nfield:u16 common_type; offset:0; size:2;\nfield:__data_loc char[] disk; offset:4; size:4;\n");
        auto bytes = value.raw;
        put(bytes, 4, static_cast<r::uint32_t>((8U << 16) | 16));
        std::memcpy(bytes.data() + 16, "nvme9n1", 8);
        check(dynamic.text(bytes, "disk") == "nvme9n1", "dynamic tracepoint string descriptor");
        put(bytes, 4, static_cast<r::uint32_t>((80U << 16) | 16));
        rejected([&] { (void)dynamic.text(bytes, "disk"); });
        for (auto text :
             {"", "ID: 70000\n", "ID: 1\nfield:u16 common_type; offset:65535; size:2;\n",
              "ID: 1\nfield:u16 common_type; size:2;\n"})
            rejected([&] { (void)b::trace_format::parse(text); });
        check(b::parse_cpu_list("0-2,4,9-10\n") == r::vector<int>{0, 1, 2, 4, 9, 10},
              "noncontiguous online CPUs");
        for (auto list : {"", "1-0", "0,0", "0,", "2147483648", "x"})
            rejected([&] { (void)b::parse_cpu_list(list); });
    }
    void correlation() {
        auto f = formats();
        auto d = device();
        r::vector<b::perf_sample> samples{
            block(2, 300),      nvme(5, 210), block(1, 100, 0, true), nvme(4, 110), block(3, 310),
            block(2, 700, 136), nvme(5, 610), block(1, 400, 8, true), nvme(4, 410)};
        samples.back().raw[40] = r::byte{17}; // 4KiB LBA 17 == 512-byte sector 136.
        auto result = b::correlate(samples, f, d, 2, 7);
        check(
            result.block.summarize().value == 200 && result.nvme.summarize().value == 100 &&
                result.block.size() == 2 && result.nvme.size() == 2,
            "cross-CPU ordering, partition normalization, duplicate completion, 4KiB LBA and reused NVMe ID yield independent series");
        auto missing = samples;
        missing.erase(missing.begin() + 1);
        rejected([&] { (void)b::correlate(missing, f, d, 2, 7); });
        auto bad = samples;
        bad.back().raw[51] = r::byte{0};
        rejected([&] { (void)b::correlate(bad, f, d, 2, 7); });
        bad = samples;
        put(bad[0].raw, 28, 1U);
        rejected([&] { (void)b::correlate(bad, f, d, 2, 7); });
        bad = samples;
        put(bad[0].raw, 24, 4U);
        rejected([&] { (void)b::correlate(bad, f, d, 2, 7); });
        bad = samples;
        put(bad.back().raw, 4, 8U);
        rejected([&] { (void)b::correlate(bad, f, d, 2, 7); });
        auto other = block(2, 50, 128);
        put(other.raw, 28, 1U);
        samples.push_back(other);
        auto unrelated = nvme(5, 50, 9);
        put(unrelated.raw, 64, static_cast<r::uint16_t>(1));
        samples.push_back(unrelated);
        result = b::correlate(samples, f, d, 2, 7);
        check(result.nvme.size() == 2,
              "unrelated failed completions do not contaminate tracked commands");
    }
    void tracing() {
        directory root;
        write(root.path / "tracing_on", "0\n");
        write(root.path / "events/block/a/enable", "0\n");
        write(root.path / "events/block/b/enable", "1\n");
        write(root.path / "events/block/enable", "X\n");
        const r::array<r::string, 1> systems{"block"};
        auto restored = [&] {
            check(read(root.path / "tracing_on") == "0" &&
                      read(root.path / "events/block/a/enable") == "0" &&
                      read(root.path / "events/block/b/enable") == "1" &&
                      read(root.path / "events/block/enable") == "X",
                  "mixed tracefs states restored without changing aggregate state");
        };
        {
            b::trace_session trace{root.path, systems};
            trace.start();
            check(read(root.path / "tracing_on") == "1" &&
                      read(root.path / "events/block/a/enable") == "1",
                  "tracefs event activation");
            check(!trace.stop(), "tracefs stop");
        }
        restored();
        try {
            b::trace_session trace{root.path, systems};
            trace.start();
            throw std::runtime_error("simulated I/O failure");
        } catch (const std::exception&) {
        }
        restored();
        {
            b::trace_session trace{root.path, systems};
            r::fs::rename(root.path / "tracing_on", root.path / "saved");
            r::fs::create_directory(root.path / "tracing_on");
            rejected([&] { trace.start(); });
            check(read(root.path / "events/block/a/enable") == "0",
                  "partial trace start rolls back changed events");
            r::fs::remove(root.path / "tracing_on");
            r::fs::rename(root.path / "saved", root.path / "tracing_on");
        }
        restored();
    }
#if RAFTPP_BENCHMARK_HAS_PERF
    r::vector<r::byte> record(const b::perf_sample& sample) {
        const auto size = (28 + sample.raw.size() + 7) & ~size_t{7};
        r::vector<r::byte> bytes(size);
        perf_event_header header{};
        header.type = PERF_RECORD_SAMPLE;
        header.size = static_cast<r::uint16_t>(size);
        std::memcpy(bytes.data(), &header, sizeof header);
        put(bytes, 8, sample.time);
        put(bytes, 16, sample.cpu);
        put(bytes, 24, static_cast<r::uint32_t>(sample.raw.size()));
        std::memcpy(bytes.data() + 28, sample.raw.data(), sample.raw.size());
        return bytes;
    }
    void append(r::aligned_buffer& mapping, r::view<const r::byte> bytes) {
        auto& control = *reinterpret_cast<perf_event_mmap_page*>(mapping.data());
        const auto size = static_cast<size_t>(control.data_size),
                   offset = static_cast<size_t>(control.data_offset);
        for (size_t i = 0; i < bytes.size(); ++i)
            mapping.data()[offset + ((control.data_head + i) & (size - 1))] = bytes[i];
        control.data_head += bytes.size();
    }
    void ring() {
        r::aligned_buffer memory{8192, 4096};
        std::fill(memory.bytes().begin(), memory.bytes().end(), r::byte{0});
        auto& control = *std::construct_at(reinterpret_cast<perf_event_mmap_page*>(memory.data()));
        control.data_offset = 4096;
        control.data_size = 4096;
        control.data_head = control.data_tail = 4092;
        auto bytes = record(block(1, 100));
        append(memory, bytes);
        auto samples = b::read_perf_ring(memory.bytes(), 4096);
        check(samples.size() == 1 && samples[0].time == 100 &&
                  samples[0].raw == block(1, 100).raw && control.data_tail == control.data_head,
              "wrapped perf record header/payload consumed and tail released");
        check(b::read_perf_ring(memory.bytes(), 4096).empty(), "empty ring remains empty");
        control.data_head = control.data_tail + 4097;
        rejected([&] { (void)b::read_perf_ring(memory.bytes(), 4096); });
        control.data_head = control.data_tail;
        perf_event_header lost{};
        lost.type = PERF_RECORD_LOST;
        lost.size = sizeof lost;
        append(memory, {reinterpret_cast<const r::byte*>(&lost), sizeof lost});
        rejected([&] { (void)b::read_perf_ring(memory.bytes(), 4096); });
        control.data_head = control.data_tail;
        bytes = record(block(1, 100));
        put(bytes, 24, 9999U);
        append(memory, bytes);
        rejected([&] { (void)b::read_perf_ring(memory.bytes(), 4096); });
        control.data_head = control.data_tail;
        control.data_size = 3000;
        rejected([&] { (void)b::read_perf_ring(memory.bytes(), 4096); });
    }
    struct fake_perf : b::perf_operations {
        int opens = 0, closes = 0, mappings = 0, fail_open = 0;
        bool fail_map = false, fail_enable = false, multiplex = false, valid = true;
        r::map<int, r::owned<r::aligned_buffer>> memory;
        int open(perf_event_attr* a, int cpu, int group) noexcept override {
            ++opens;
            if (opens == fail_open) {
                errno = EACCES;
                return -1;
            }
            valid &= cpu >= 0 && a->type == PERF_TYPE_TRACEPOINT && a->sample_period == 1 &&
                     a->use_clockid &&
                     a->sample_type == (PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_RAW) &&
                     ((group < 0) == static_cast<bool>(a->disabled));
            return opens + 100;
        }
        void* map(size_t size, int fd) noexcept override {
            if (fail_map) {
                errno = ENOMEM;
                return MAP_FAILED;
            }
            try {
                auto value = std::make_unique<r::aligned_buffer>(size, 4096);
                std::fill(value->bytes().begin(), value->bytes().end(), r::byte{0});
                auto& header =
                    *std::construct_at(reinterpret_cast<perf_event_mmap_page*>(value->data()));
                header.data_offset = 4096;
                header.data_size = size - 4096;
                auto address = value->data();
                memory.emplace(fd, std::move(value));
                ++mappings;
                return address;
            } catch (...) {
                errno = ENOMEM;
                return MAP_FAILED;
            }
        }
        int unmap(void* address, size_t) noexcept override {
            for (auto i = memory.begin(); i != memory.end(); ++i)
                if (i->second->data() == address) {
                    memory.erase(i);
                    --mappings;
                    return 0;
                }
            valid = false;
            return -1;
        }
        int close(int) noexcept override {
            ++closes;
            return 0;
        }
        int control(int, unsigned long request, unsigned long arg) noexcept override {
            if (request == PERF_EVENT_IOC_ENABLE && fail_enable) {
                errno = EIO;
                return -1;
            }
            valid &= request == PERF_EVENT_IOC_SET_OUTPUT || arg == PERF_IOC_FLAG_GROUP;
            return 0;
        }
        ssize_t read(int, void* data, size_t size) noexcept override {
            r::array<r::uint64_t, 3> value{10, 100, multiplex ? 50U : 100U};
            if (size != sizeof value) {
                valid = false;
                return -1;
            }
            std::memcpy(data, value.data(), size);
            return static_cast<ssize_t>(size);
        }
        r::uint64_t switches() override {
            return 0;
        }
        void clean(int failed = 0) const {
            check(valid && mappings == 0 && memory.empty() && closes == opens - failed,
                  "perf descriptors/mappings clean after success or failure");
        }
    };
    void perf() {
        auto f = formats();
        f.resize(3);
        for (int stage = 1; stage <= 3; ++stage) {
            fake_perf api;
            api.fail_open = stage;
            rejected([&] { b::perf_group group{0, f, 4096, api}; });
            api.clean(1);
        }
        {
            fake_perf api;
            api.fail_map = true;
            rejected([&] { b::perf_group group{0, f, 4096, api}; });
            api.clean();
        }
        {
            fake_perf api;
            api.fail_enable = true;
            {
                b::perf_group group{0, f, 4096, api};
                rejected([&] { group.start(); });
            }
            api.clean();
        }
        {
            fake_perf api;
            api.multiplex = true;
            {
                b::perf_group group{0, f, 4096, api};
                group.start();
                rejected([&] { group.stop(); });
            }
            api.clean();
        }
        directory root;
        write(root.path / "events/block/block_bio_queue/format", format(1));
        write(root.path / "events/block/block_rq_complete/format", format(2));
        write(root.path / "events/block/block_bio_complete/format", format(3));
        write(root.path / "events/nvme/nvme_setup_cmd/format", format(4, true));
        write(root.path / "events/nvme/nvme_complete_rq/format", format(5, true));
        fake_perf api;
        {
            b::kernel_profiler profiler{device(), true, {}, root.path, api, {0, 2}};
            profiler.start();
            check(api.memory.size() == 2, "one shared output mapping per noncontiguous CPU");
            auto first = api.memory.begin();
            auto second = std::next(first);
            for (auto sample : {block(2, 300), nvme(5, 210)}) {
                const auto bytes = record(sample);
                append(*first->second, bytes);
            }
            for (auto sample : {block(1, 100, 0, true), nvme(4, 110)}) {
                const auto bytes = record(sample);
                append(*second->second, bytes);
            }
            profiler.poll();
            profiler.stop();
            b::report result;
            profiler.report_into(result, 4096, 1, 7);
            const auto json = result.json();
            check(json.find("disk:block:4096") != r::string::npos &&
                      json.find("disk:nvme:4096") != r::string::npos &&
                      json.find("\"value\": 200") != r::string::npos &&
                      json.find("\"value\": 100") != r::string::npos,
                  "full collector reports independent block/NVMe samples");
        }
        api.clean();
    }
#endif
    int native() {
#if RAFTPP_BENCHMARK_HAS_PERF
        try {
            const r::fs::path root{"/sys/kernel/tracing"};
            std::ifstream input{root / "events/block/block_bio_queue/format"};
            if (!input) {
                std::cout
                    << "SKIP: native tracepoint format unavailable; host tracing was not changed\n";
                return 77;
            }
            r::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
            const r::array<b::event_format, 1> formats{
                {{b::kernel_event::block_queue, b::trace_format::parse(text)}}};
            std::ifstream online{"/sys/devices/system/cpu/online"};
            r::string list;
            std::getline(online, list);
            b::perf_group group{b::parse_cpu_list(list).front(), formats,
                                static_cast<size_t>(sysconf(_SC_PAGESIZE)),
                                b::native_perf_operations()};
            check(!group.close(), "native disabled perf descriptor/mapping cleanup");
            std::cout
                << "Native perf initialization only; no host trace enabled or hardware metrics claimed\n";
        } catch (const r::system_error& error) {
            if (error.code() == std::errc::permission_denied ||
                error.code() == std::errc::operation_not_permitted ||
                error.code() == std::errc::function_not_supported) {
                std::cout << "SKIP: native perf unavailable: " << error.what() << '\n';
                return 77;
            }
            throw;
        }
        return 0;
#else
        std::cout << "SKIP: perf headers unavailable\n";
        return 77;
#endif
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "profiler suite argument");
        const r::string_view suite{argv[1]};
        if (suite == "options")
            options();
        else if (suite == "format")
            field_format();
        else if (suite == "correlation")
            correlation();
        else if (suite == "trace")
            tracing();
        else if (suite == "native")
            return native();
#if RAFTPP_BENCHMARK_HAS_PERF
        else if (suite == "ring")
            ring();
        else if (suite == "perf")
            perf();
#else
        else if (suite == "ring" || suite == "perf")
            return 77;
#endif
        else
            check(false, "unknown profiler suite");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
