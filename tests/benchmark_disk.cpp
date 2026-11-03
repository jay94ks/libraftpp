#include "../tools/benchmark/disk.hpp"
#include "../tools/benchmark/uring.hpp"
#include <bit>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
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
        check(failed, "invalid input/operation rejected");
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-benchmark-disk-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "create private test directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
        void marker() const {
            std::ofstream{path / "preserved"} << "unrelated data";
        }
        void intact() const {
            size_t count = 0;
            for (const auto& entry : r::fs::directory_iterator{path}) {
                check(entry.path().filename() == "preserved", "private benchmark data removed");
                ++count;
            }
            r::string data;
            std::ifstream input{path / "preserved"};
            std::getline(input, data);
            check(count == 1 && data == "unrelated data", "unrelated parent contents preserved");
        }
    };
    void options() {
        const auto defaults = b::parse_disk({});
        check(defaults.buffer == 4096 && defaults.size == 8388608 && defaults.operations() == 2048,
              "C disk defaults");
        const r::array<r::string_view, 5> args{"-b512", "--size=1K", "--timeout=123", "--dir",
                                               "path with spaces"};
        const auto parsed = b::parse_disk(args);
        check(parsed.buffer == 512 && parsed.size == 1024 && parsed.timeout == 123 &&
                  parsed.directory == "path with spaces",
              "disk option ownership and forms");
        for (auto value : {"0", "1", "511", "513", "1536", "2097152", "18446744073709551615"}) {
            const r::array<r::string_view, 2> values{"--buf", value};
            rejected([&] { (void)b::parse_disk(values); });
        }
        for (auto value : {"0", "4097", "4294967296"}) {
            const r::array<r::string_view, 2> values{"--size", value};
            rejected([&] { (void)b::parse_disk(values); });
        }
        for (auto value : {"0", "-1", "1K", "4294967296"}) {
            const r::array<r::string_view, 2> values{"--timeout", value};
            rejected([&] { (void)b::parse_disk(values); });
        }
        const r::array<r::string_view, 1> missing{"--buf"};
        rejected([&] { (void)b::parse_disk(missing); });
        const r::array<r::string_view, 2> unknown{"--address", "anything"};
        rejected([&] { (void)b::parse_disk(unknown); });
        auto invalid = defaults;
        invalid.buffer = 0;
        rejected([&] { (void)b::disk(invalid); });
        const r::array<r::string_view, 1> help{"--help"};
        check(b::parse_disk(help).help, "disk help");
    }
    void write(const r::fs::path& path, r::string_view text) {
        std::ofstream out{path};
        out << text;
        check(static_cast<bool>(out), "write synthetic sysfs fixture");
    }
    void device() {
        directory root;
        struct stat status {};
        check(stat(root.path.c_str(), &status) == 0, "stat fixture filesystem");
        const auto sysfs = root.path / "sys";
        r::fs::create_directories(sysfs);
        const auto link = sysfs / (std::to_string(major(status.st_dev)) + ":" +
                                   std::to_string(minor(status.st_dev)));
        auto generic = b::inspect_device(root.path, sysfs);
        check(generic.driver == b::device_driver::generic && !generic.raw &&
                  generic.first_sector == 0 && !generic.write_through,
              "missing device entry has initialized generic metadata");
        const auto parent = root.path / "nvme0n1";
        const auto partition = parent / "nvme0n1p9";
        r::fs::create_directories(partition);
        r::fs::create_directories(parent / "queue");
        write(partition / "start", "4294967296\n");
        write(partition / "size", "4294967296\n");
        write(partition / "partition", "9\n");
        write(parent / "dev", "259:0\n");
        write(parent / "queue/logical_block_size", "4096\n");
        write(parent / "queue/write_cache", "write through\n");
        r::fs::create_directory_symlink(partition, link);
        auto info = b::inspect_device(root.path, sysfs);
        check(info.driver == b::device_driver::nvme && info.first_sector == 4294967296ULL &&
                  info.end_sector == 8589934592ULL && info.write_through,
              "64-bit partition sectors and parent queue write-through");
        check(info.disk_name == "nvme0n1" && info.parent_device == makedev(259, 0) &&
                  info.logical_block_size == 4096,
              "NVMe namespace, parent device and logical block metadata");
        write(parent / "queue/write_cache", "write back\n");
        check(!b::inspect_device(root.path, sysfs).write_through, "write-back policy");
        write(partition / "size", "18446744073709551615\n");
        rejected([&] { (void)b::inspect_device(root.path, sysfs); });
        write(partition / "size", "12garbage\n");
        rejected([&] { (void)b::inspect_device(root.path, sysfs); });
        write(partition / "size", "12\n");
        write(parent / "queue/write_cache", "unrecognized\n");
        rejected([&] { (void)b::inspect_device(root.path, sysfs); });
        r::fs::remove(link);
        const auto nullb = root.path / "nullb7";
        r::fs::create_directories(nullb / "queue");
        write(nullb / "size", "1024\n");
        write(nullb / "queue/write_cache", "write through\n");
        r::fs::create_directory_symlink(nullb, link);
        info = b::inspect_device(root.path, sysfs);
        check(info.driver == b::device_driver::null_block && info.first_sector == 0 &&
                  info.end_sector == 1024 && info.write_through,
              "whole null-block device without start file");
    }
    void target() {
        directory root;
        root.marker();
        b::disk_options settings;
        settings.directory = root.path;
        settings.size = 8192;
        int descriptor;
        {
            b::disk_target output{settings};
            descriptor = output.descriptor();
            output.prepare();
            check(!output.info().raw && r::fs::file_size(output.path()) == settings.size,
                  "private file preallocated and materialized");
            r::vector<char> data(static_cast<size_t>(settings.size));
            std::ifstream input{output.path(), std::ios::binary};
            input.read(data.data(), static_cast<std::streamsize>(data.size()));
            check(input.gcount() == static_cast<std::streamsize>(data.size()) &&
                      std::all_of(data.begin(), data.end(), [](char c) { return c == 0; }),
                  "prefill writes initialized aligned zeros");
            output.cleanup();
            check(fcntl(descriptor, F_GETFD) == -1 && errno == EBADF,
                  "explicit cleanup closes descriptor");
        }
        root.intact();
        {
            b::disk_target output{settings};
            volatile std::sig_atomic_t stop = 1;
            rejected([&] { output.prepare(&stop); });
        }
        root.intact();
        settings.directory = root.path / "preserved";
        rejected([&] { b::disk_target invalid{settings}; });
        root.intact();
    }
#if RAFTPP_BENCHMARK_HAS_IO_URING
    struct fake_uring : b::uring_operations {
        alignas(64) r::array<r::byte, 4096> sq{}, cq{}, sqes{};
        bool shared = true, defer = false, interrupt_before = false, interrupt_after = false;
        bool valid = true, files = false, buffers = false, outstanding = false, closed = false;
        int fail_setup = 0, fail_map = 0, fail_register = 0, enter_error = 0,
            completion_result = 4096;
        bool zero_submit = false, wrong_token = false;
        unsigned maps = 0, live_maps = 0, registrations = 0, drain_calls = 0, writes = 0;
        r::array<r::uint64_t, 16> offsets{};
        r::borrowed<const volatile std::sig_atomic_t> stop = nullptr;
        r::borrowed<volatile std::sig_atomic_t> trigger_stop = nullptr;
        io_uring_params params{};
        iovec buffer{};
        unsigned& s(unsigned offset) {
            return *reinterpret_cast<unsigned*>(sq.data() + offset);
        }
        unsigned& c(unsigned offset) {
            return *reinterpret_cast<unsigned*>((shared ? sq.data() : cq.data()) + offset);
        }
        io_uring_sqe& entry() {
            return reinterpret_cast<io_uring_sqe*>(sqes.data())[s(0) & 1];
        }
        int setup(unsigned count, io_uring_params* value) noexcept override {
            if (fail_setup) {
                errno = fail_setup;
                return -1;
            }
            valid &= count == 1 && value->flags == 0;
            params.sq_entries = params.cq_entries = 2;
            params.features = shared ? IORING_FEAT_SINGLE_MMAP : 0;
            params.sq_off.head = 0;
            params.sq_off.tail = 4;
            params.sq_off.ring_mask = 8;
            params.sq_off.array = 64;
            params.cq_off.head = 128;
            params.cq_off.tail = 132;
            params.cq_off.ring_mask = 136;
            params.cq_off.cqes = 256;
            s(0) = s(4) = c(128) = c(132) = r::numeric_limits<unsigned>::max() - 1;
            s(8) = c(136) = 1;
            *value = params;
            return 42;
        }
        void* map(size_t, int fd, off_t offset) noexcept override {
            valid &= fd == 42;
            ++maps;
            if (static_cast<int>(maps) == fail_map) {
                errno = ENOMEM;
                return MAP_FAILED;
            }
            ++live_maps;
            return offset == IORING_OFF_SQES      ? sqes.data()
                   : offset == IORING_OFF_CQ_RING ? cq.data()
                                                  : sq.data();
        }
        int unmap(void*, size_t) noexcept override {
            valid &= !outstanding;
            --live_maps;
            return 0;
        }
        int register_resources(int fd, unsigned op, const void* data,
                               unsigned count) noexcept override {
            valid &= fd == 42;
            if (op == IORING_REGISTER_FILES || op == IORING_REGISTER_BUFFERS) {
                if (static_cast<int>(++registrations) == fail_register) {
                    errno = ENOMEM;
                    return -1;
                }
                valid &= count == 1;
                if (op == IORING_REGISTER_FILES) {
                    valid &= *static_cast<const int*>(data) == 17;
                    files = true;
                } else {
                    buffer = *static_cast<const iovec*>(data);
                    buffers = true;
                    valid &= reinterpret_cast<r::uintptr_t>(buffer.iov_base) % 4096 == 0 &&
                             buffer.iov_len == 4096;
                    for (size_t i = 0; i < buffer.iov_len; ++i)
                        valid &= static_cast<unsigned char*>(buffer.iov_base)[i] == i % 128;
                }
            } else {
                valid &= !outstanding;
                if (op == IORING_UNREGISTER_BUFFERS)
                    buffers = false;
                else if (op == IORING_UNREGISTER_FILES)
                    files = false;
                else
                    valid = false;
            }
            return 0;
        }
        void complete() noexcept {
            auto& result =
                reinterpret_cast<io_uring_cqe*>((shared ? sq.data() : cq.data()) + 256)[c(132) & 1];
            result.res = completion_result;
            result.user_data =
                reinterpret_cast<io_uring_sqe*>(sqes.data())[(s(0) - 1) & 1].user_data +
                (wrong_token ? 1 : 0);
            ++c(132);
            outstanding = false;
        }
        int enter(int fd, unsigned submit, unsigned minimum, unsigned flags) noexcept override {
            valid &= fd == 42 && flags == IORING_ENTER_GETEVENTS;
            if (submit) {
                if (interrupt_before) {
                    interrupt_before = false;
                    errno = EINTR;
                    return -1;
                }
                if (enter_error) {
                    errno = enter_error;
                    return -1;
                }
                if (zero_submit)
                    return 0;
                auto& request = entry();
                valid &= files && buffers && request.opcode == IORING_OP_WRITE_FIXED &&
                         request.flags == IOSQE_FIXED_FILE && request.fd == 0 &&
                         request.buf_index == 0 && request.rw_flags == RWF_DSYNC &&
                         request.addr == reinterpret_cast<r::uintptr_t>(buffer.iov_base) &&
                         request.len == 4096;
                if (writes < offsets.size())
                    offsets[writes] = request.off;
                ++writes;
                ++s(0);
                outstanding = true;
                if (trigger_stop)
                    *trigger_stop = 1;
                if (!defer)
                    complete();
                if (interrupt_after) {
                    interrupt_after = false;
                    errno = EINTR;
                    return -1;
                }
                return 1;
            }
            if (minimum) {
                ++drain_calls;
                if (outstanding)
                    complete();
            } else if (!defer && outstanding)
                complete();
            return 0;
        }
        int close(int fd) noexcept override {
            valid &=
                fd == 42 && !outstanding && !files && !buffers && live_maps == 0 && s(0) == s(4);
            closed = true;
            return 0;
        }
        void cleaned() const {
            check(valid && closed && live_maps == 0 && !files && !buffers && !outstanding,
                  "ring FD/mappings/registrations/buffer lifetime cleaned");
        }
    };
    void ring() {
        {
            fake_uring api;
            api.fail_setup = EPERM;
            rejected([&] { b::uring_writer writer{17, 4096, api}; });
            check(!api.closed && api.maps == 0 && api.registrations == 0,
                  "setup denial owns no ring resources");
        }
        for (bool shared : {true, false}) {
            fake_uring api;
            api.shared = shared;
            {
                b::uring_writer writer{17, 4096, api};
                api.interrupt_before = true;
                for (r::uint64_t offset = 0; offset < 4 * 4096; offset += 4096)
                    writer.write(offset, b::clock::now() + r::chrono::seconds{1});
                api.interrupt_after = true;
                writer.write(4 * 4096, b::clock::now() + r::chrono::seconds{1});
                check(api.writes == 5 && api.offsets[4] == 4 * 4096,
                      "fixed writes survive SQ/CQ wrap and interrupted submits");
                check(!writer.close() && !writer.close(), "ring explicit close is idempotent");
                rejected([&] { writer.write(0, b::clock::now() + r::chrono::seconds{1}); });
            }
            api.cleaned();
        }
        for (int stage = 1; stage <= 3; ++stage) {
            fake_uring api;
            api.shared = false;
            api.fail_map = stage;
            rejected([&] { b::uring_writer writer{17, 4096, api}; });
            api.cleaned();
        }
        for (int stage = 1; stage <= 2; ++stage) {
            fake_uring api;
            api.fail_register = stage;
            rejected([&] { b::uring_writer writer{17, 4096, api}; });
            api.cleaned();
        }
        for (int result : {-EIO, 2048, r::numeric_limits<int>::min()}) {
            fake_uring api;
            api.completion_result = result;
            {
                b::uring_writer writer{17, 4096, api};
                rejected([&] { writer.write(0, b::clock::now() + r::chrono::seconds{1}); });
            }
            api.cleaned();
        }
        for (bool zero : {true, false}) {
            fake_uring api;
            api.zero_submit = zero;
            api.enter_error = zero ? 0 : EACCES;
            {
                b::uring_writer writer{17, 4096, api};
                rejected([&] { writer.write(0, b::clock::now() + r::chrono::seconds{1}); });
            }
            api.cleaned();
        }
        {
            fake_uring api;
            api.defer = true;
            {
                b::uring_writer writer{17, 4096, api};
                rejected([&] { writer.write(0, b::clock::now() + r::chrono::milliseconds{1}); });
            }
            api.cleaned();
            check(api.drain_calls == 1, "timeout drains accepted write before releasing buffers");
        }
        {
            fake_uring api;
            api.defer = true;
            volatile std::sig_atomic_t stop = 0;
            api.trigger_stop = &stop;
            {
                b::uring_writer writer{17, 4096, api};
                rejected([&] { writer.write(0, b::clock::now() + r::chrono::seconds{1}, &stop); });
            }
            api.cleaned();
            check(api.drain_calls == 1, "interruption drains accepted write");
        }
        {
            fake_uring api;
            {
                b::uring_writer writer{17, 4096, api};
                rejected([&] {
                    writer.write(r::numeric_limits<r::uint64_t>::max(),
                                 b::clock::now() + r::chrono::seconds{1});
                });
            }
            api.cleaned();
        }
    }
#endif
    int native() {
        directory root;
        root.marker();
        b::disk_options settings;
        settings.directory = root.path;
        settings.size = 8192;
        settings.timeout = 5000;
        try {
            const auto output = b::disk(settings).json();
            check(output.find("\"disk:4096\"") != r::string::npos &&
                      output.find("\"throughput\"") != r::string::npos,
                  "actual io_uring produces latency and MiB/s metrics");
        } catch (const r::system_error& error) {
            root.intact();
            const r::string_view message{error.what()};
            if ((message.find("io_uring_setup") != r::string_view::npos &&
                 (error.code() == std::errc::operation_not_permitted ||
                  error.code() == std::errc::permission_denied ||
                  error.code() == std::errc::function_not_supported)) ||
                error.code() == std::errc::operation_not_supported) {
                std::cout << "SKIP: actual kernel io_uring unavailable: " << error.what() << '\n';
                return 77;
            }
            throw;
        }
        root.intact();
        return 0;
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "disk suite argument");
        const r::string_view suite{argv[1]};
        if (suite == "options")
            options();
        else if (suite == "device")
            device();
        else if (suite == "target")
            target();
        else if (suite == "ring") {
#if RAFTPP_BENCHMARK_HAS_IO_URING
            ring();
#else
            std::cout << "SKIP: io_uring headers unavailable\n";
            return 77;
#endif
        } else if (suite == "native")
            return native();
        else
            check(false, "unknown disk suite");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
