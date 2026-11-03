#include <raftpp.hpp>
#include "byte.hpp"
#include "disk.hpp"

#include <algorithm>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>

namespace r = raftpp;
namespace d = raftpp::detail;

namespace {
    void check(bool condition, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }

    // LZ4F_compressFrame with default preferences, generated from a little-endian
    // count of 41 followed by 4088 bytes of 0xa5. Keep the same valid compressed
    // input in LZ4-OFF builds without linking a compressor into the test.
    constexpr r::array<unsigned char, 46> compressed{
        0x04, 0x22, 0x4d, 0x18, 0x60, 0x40, 0x82, 0x1f, 0x00, 0x00, 0x00, 0x22,
        0x29, 0x00, 0x01, 0x00, 0x1f, 0xa5, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xee,
        0x50, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0x00, 0x00, 0x00, 0x00};
    constexpr r::size_t payload_size = 4096;
    constexpr r::log_index snapshot_index = 20;

    struct directory {
        r::fs::path path;
        directory() {
            r::string pattern = "/tmp/raftpp-compressed-transfer-XXXXXX";
            check(mkdtemp(pattern.data()), "temporary directory");
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
            check(!uv_loop_init(&value), "loop init");
        }
        ~loop() {
            if (uv_loop_close(&value))
                std::abort();
        }
        template <typename F>
        void until(F condition, std::source_location where = std::source_location::current()) {
            const auto deadline = uv_hrtime() + 12000000000ULL;
            while (!condition() && uv_hrtime() < deadline) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            check(condition(), "operation completed before timeout", where);
        }
    };

    r::string available_address() {
        d::file_handle socket{::socket(AF_INET, SOCK_STREAM, 0)};
        check(socket.get() >= 0, "socket");
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(!::bind(socket.get(), reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)),
              "reserve port");
        socklen_t size = sizeof(endpoint);
        check(!::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&endpoint), &size),
              "reserved endpoint");
        return "127.0.0.1:" + std::to_string(ntohs(endpoint.sin_port));
    }

    struct machine final : r::state_machine {
        r::uint64_t count = 0;
        unsigned restores = 0;
        r::error_code apply(r::view<const r::byte>, r::shared<void>&) override {
            ++count;
            return {};
        }
        r::error_code restore(r::shared_buffer input) override {
            if (input.size() != payload_size ||
                !std::ranges::all_of(input.bytes().subspan(8),
                                     [](auto byte) { return byte == r::byte{0xa5}; }))
                return r::errc::corrupt;
            count = d::load_little_endian<r::uint64_t>(input.bytes().first<8>());
            ++restores;
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>&) override {
            return r::errc::io_error;
        }
    };

    struct cluster {
        loop event_loop;
        r::array<directory, 3> directories;
        r::array<r::string, 3> addresses;
        r::configuration members;
        r::array<r::shared<r::uv_io>, 3> backends;
        r::array<r::shared<machine>, 3> machines;
        r::array<r::owned<r::node>, 3> nodes;
        unsigned completed = 0, closed = 0;
        r::error_code completion_error;
        cluster() {
            for (unsigned i = 0; i < 3; ++i) {
                do {
                    addresses[i] = available_address();
                } while (std::find(addresses.begin(), addresses.begin() + i, addresses[i]) !=
                         addresses.begin() + i);
                check(!members.add(i + 1, addresses[i], r::role::voter), "cluster member");
            }
        }
        ~cluster() {
            for (auto& node : nodes)
                if (node)
                    (void)node->close();
            for (auto& backend : backends)
                if (backend)
                    backend->close({});
            (void)uv_run(&event_loop.value, UV_RUN_DEFAULT);
        }
        r::fs::path seed(unsigned i) {
            d::disk_storage disk{directories[i].path};
            check(!disk.open() && !disk.set_term(3), "seed metadata");
            r::buffer bytes{compressed.size()};
            std::transform(compressed.begin(), compressed.end(), bytes.data(),
                           [](auto value) { return static_cast<r::byte>(value); });
            r::snapshot value;
            value.index = snapshot_index;
            value.term = 3;
            value.configuration = members;
            value.configuration_index = 1;
            value.buffers.push_back(r::shared_buffer{std::move(bytes)});
            d::prepared_snapshot prepared;
            check(!disk.prepare_snapshot(value, 1, prepared) && !disk.commit_snapshot(prepared),
                  "persist compressed snapshot");
            r::vector<d::snapshot_file> files;
            check(!disk.list_snapshots(files) && files.size() == 1, "one seeded snapshot");
            return directories[i].path / files[0].filename.substr(0, files[0].filename.size() - 5);
        }
        r::error_code open(unsigned i, bool bootstrap = false) {
            r::tcp_transport_options transport_options;
            transport_options.connect_retry = 10;
            transport_options.tracing = r::tracer::noop();
            auto transport =
                std::make_shared<r::tcp_transport>(event_loop.value, transport_options);
            r::uv_io_options io_options;
            io_options.disk_retry = 1;
            io_options.segment_size = 8192;
            io_options.max_snapshot_size = 8192;
            io_options.tracing = r::tracer::noop();
            backends[i] = std::make_shared<r::uv_io>(event_loop.value, directories[i].path,
                                                     transport, io_options);
            machines[i] = std::make_shared<machine>();
            r::node_options options;
            options.capacity_threshold = 0;
            options.heartbeat_timeout = 20;
            options.election_timeout = 1000;
            options.snapshot_threshold = 10000;
            options.tracing = r::tracer::noop();
            nodes[i] =
                std::make_unique<r::node>(i + 1, addresses[i], backends[i], machines[i], options);
            if (bootstrap)
                check(!nodes[i]->bootstrap(members), "bootstrap lagging node");
            return nodes[i]->start();
        }
        void healthy() {
            for (const auto& node : nodes)
                if (node)
                    check(!node->error(), "healthy cluster node");
            check(!completion_error, "successful client completion");
        }
        unsigned leader() {
            for (unsigned i = 0; i < nodes.size(); ++i)
                if (nodes[i] && nodes[i]->state() == r::node_state::leader)
                    return i;
            return static_cast<unsigned>(nodes.size());
        }
        void apply() {
            event_loop.until([&] {
                healthy();
                return leader() < nodes.size();
            });
            const auto primary = leader();
            const auto target = completed + 1;
            check(primary < nodes.size() && !nodes[primary]->apply(r::buffer{8},
                                                                   [this](auto status, auto) {
                                                                       completion_error = status;
                                                                       ++completed;
                                                                   }),
                  "submit command");
            event_loop.until([&] {
                healthy();
                return completed == target;
            });
        }
        void stop() {
            closed = 0;
            unsigned expected = 0;
            for (auto& node : nodes)
                if (node) {
                    ++expected;
                    check(!node->close([this] { ++closed; }), "close node");
                }
            event_loop.until([&] { return closed == expected; });
            for (unsigned i = 0; i < nodes.size(); ++i) {
                nodes[i].reset();
                backends[i].reset();
                machines[i].reset();
            }
        }
    };

    r::vector<unsigned char> read_file(const r::fs::path& path) {
        std::ifstream input{path, std::ios::binary};
        check(input.is_open(), "snapshot data file opens");
        return {std::istreambuf_iterator<char>{input}, {}};
    }

    void original_frame(const r::fs::path& path) {
        const auto bytes = read_file(path);
        check(bytes.size() == compressed.size() && std::ranges::equal(bytes, compressed),
              "source snapshot keeps its original compressed bytes");
    }

    void run() {
        cluster value;
        r::array<r::fs::path, 2> sources{value.seed(0), value.seed(1)};
        if constexpr (!r::features::lz4) {
            for (unsigned i = 0; i < sources.size(); ++i) {
                check(value.open(i) == r::errc::invalid && value.machines[i]->restores == 0,
                      "LZ4 OFF rejects a valid compressed snapshot before FSM restore");
            }
            value.stop();
            for (const auto& path : sources)
                original_frame(path);
            std::cout
                << "LZ4 OFF: two startup rejections, no FSM restore, source frames preserved\n";
            return;
        }
        for (unsigned i = 0; i < sources.size(); ++i)
            check(!value.open(i) && value.machines[i]->count == 41 &&
                      value.machines[i]->restores == 1,
                  "startup restores the decompressed 4096-byte snapshot");
        value.apply();
        value.event_loop.until([&] {
            value.healthy();
            return value.machines[0]->count == 42 && value.machines[1]->count == 42;
        });
        check(!value.open(2, true) && value.machines[2]->restores == 0,
              "lagger starts without a snapshot");
        value.event_loop.until([&] {
            value.healthy();
            return value.machines[2]->count == 42 && value.machines[2]->restores == 1;
        });
        value.stop();
        for (const auto& path : sources)
            original_frame(path);
        d::disk_storage disk{value.directories[2].path};
        check(!disk.open(), "receiver disk");
        r::optional<r::snapshot> installed;
        check(!disk.latest_snapshot(installed) && installed && installed->index == snapshot_index &&
                  installed->buffers.size() == 1 && installed->buffers[0].size() == payload_size &&
                  !r::is_compressed(installed->buffers[0].bytes()),
              "receiver persisted the decoded TCP snapshot");
        r::vector<d::snapshot_file> files;
        check(!disk.list_snapshots(files) && files.size() == 1, "one receiver snapshot");
        const auto raw = read_file(value.directories[2].path /
                                   files[0].filename.substr(0, files[0].filename.size() - 5));
        check(
            raw.size() == payload_size &&
                std::equal(raw.begin(), raw.end(), installed->buffers[0].bytes().begin(),
                           installed->buffers[0].bytes().end(),
                           [](unsigned char left, r::byte right) {
                               return static_cast<r::byte>(left) == right;
                           }),
            "receiver file contains the exact decompressed payload, not another compressed frame");
        for (unsigned i = 0; i < value.nodes.size(); ++i)
            check(!value.open(i) && value.machines[i]->restores == 1,
                  "restart restores every persisted snapshot");
        value.event_loop.until([&] {
            value.healthy();
            return std::ranges::all_of(value.machines,
                                       [](auto& machine) { return machine->count == 42; });
        });
        value.apply();
        value.event_loop.until([&] {
            value.healthy();
            return std::ranges::all_of(value.machines,
                                       [](auto& machine) { return machine->count == 43; });
        });
        value.stop();
        for (const auto& path : sources)
            original_frame(path);
        std::cout
            << "LZ4 ON: two compressed startup restores, native TCP snapshot catch-up, receiver persistence, "
               "three-node restart, two committed commands, source frames preserved\n";
    }
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
