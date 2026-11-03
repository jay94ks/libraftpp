#include <raftpp/common.hpp>
#include "../examples/counter.hpp"
#include <cstring>
#include <uv.h>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <sys/socket.h>
#include <unistd.h>

namespace r = raftpp;
namespace {
    void check(bool value, r::string_view message) {
        if (!value)
            throw std::runtime_error(r::string{message});
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-examples-XXXXXX";
            check(mkdtemp(name.data()) != nullptr, "create test directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    struct socket {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        socket() = default;
        socket(const socket&) = delete;
        socket& operator=(const socket&) = delete;
        ~socket() {
            if (fd >= 0)
                ::close(fd);
        }
        bool bind(unsigned port) {
            if (fd < 0)
                return false;
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(static_cast<uint16_t>(port));
            return !::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        }
    };
    unsigned available_base() {
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            const auto port = 30000 + static_cast<unsigned>((uv_hrtime() + attempt * 3) % 30000);
            r::array<socket, 3> sockets;
            if (sockets[0].bind(port + 1) && sockets[1].bind(port + 2) && sockets[2].bind(port + 3))
                return port;
        }
        throw std::runtime_error("reserve three available example ports");
    }
    struct file_closer {
        void operator()(FILE* file) const noexcept {
            if (file)
                std::fclose(file);
        }
    };
    struct result {
        int64_t status = -1;
        int signal = -1;
        bool timed_out = false;
        r::string output;
    };
    result run(r::vector<r::string> arguments, const r::fs::path& log, unsigned timeout = 10000) {
        result output;
        r::owned<FILE, file_closer> file{std::fopen(log.c_str(), "w")};
        check(bool(file), "open child output");
        uv_loop_t loop{};
        check(!uv_loop_init(&loop), "process loop init");
        struct state {
            uv_process_t process{};
            uv_timer_t timer{};
            result* output;
        } state;
        state.output = &output;
        state.process.data = state.timer.data = &state;
        check(!uv_timer_init(&loop, &state.timer), "process timeout init");
        r::vector<char*> argv;
        for (auto& argument : arguments)
            argv.push_back(argument.data());
        argv.push_back(nullptr);
        r::array<uv_stdio_container_t, 3> stdio{};
        for (unsigned i = 0; i < stdio.size(); ++i) {
            stdio[i].flags = UV_INHERIT_FD;
            stdio[i].data.fd = i == 0 ? 0 : fileno(file.get());
        }
        uv_process_options_t options{};
        options.file = argv.front();
        options.args = argv.data();
        options.flags =
            UV_PROCESS_DETACHED; // Owned process group for timeout cleanup, including grandchildren.
        options.stdio_count = static_cast<int>(stdio.size());
        options.stdio = stdio.data();
        options.exit_cb = [](uv_process_t* native, int64_t status, int signal) noexcept {
            auto& state = *static_cast<struct state*>(native->data);
            state.output->status = status;
            state.output->signal = signal;
            uv_close(reinterpret_cast<uv_handle_t*>(&state.process), nullptr);
            uv_close(reinterpret_cast<uv_handle_t*>(&state.timer), nullptr);
        };
        const auto spawned = uv_spawn(&loop, &state.process, &options);
        state.process.data = &state;
        if (spawned) {
            if (state.process.type == UV_PROCESS)
                uv_close(reinterpret_cast<uv_handle_t*>(&state.process), nullptr);
            uv_close(reinterpret_cast<uv_handle_t*>(&state.timer), nullptr);
        } else {
            check(!uv_timer_start(
                      &state.timer,
                      [](uv_timer_t* native) noexcept {
                          auto& state = *static_cast<struct state*>(native->data);
                          state.output->timed_out = true;
                          (void)::kill(-state.process.pid, SIGKILL);
                      },
                      timeout, 0),
                  "process timeout start");
        }
        (void)uv_run(&loop, UV_RUN_DEFAULT);
        check(!uv_loop_close(&loop), "process loop drained");
        file.reset();
        std::ifstream input{log};
        output.output.assign(std::istreambuf_iterator<char>{input}, {});
        if (spawned || output.timed_out || output.status != 0 || output.signal)
            std::cerr << output.output;
        check(!spawned && !output.timed_out, "example child started and completed before timeout");
        return output;
    }
    void counter() {
        r::shared<void> result;
        r::vector<r::shared_buffer> snapshot;
        {
            r::example::counter machine;
            r::array<r::byte, sizeof(r::uint64_t)> bytes{};
            const r::uint64_t delta = 42;
            std::memcpy(bytes.data(), &delta, sizeof(delta));
            check(!machine.apply(bytes, result) && machine.value() == 42,
                  "counter consumes the C example's uint64 command");
            check(machine.apply({}, result) == r::errc::malformed && machine.value() == 42,
                  "malformed commands preserve counter value");
            check(!machine.snapshot(snapshot) && snapshot.size() == 1 &&
                      snapshot[0].size() == sizeof(delta),
                  "counter produces an owning snapshot");
            check(!machine.apply(bytes, result) && machine.value() == 84, "second counter command");
            r::uint64_t saved;
            std::memcpy(&saved, snapshot[0].bytes().data(), sizeof(saved));
            check(saved == 42, "snapshot bytes do not alias mutable state");
            check(machine.restore({}) == r::errc::malformed && machine.value() == 84,
                  "malformed restore preserves state");
        }
        check(*std::static_pointer_cast<r::uint64_t>(result) == 84,
              "FSM result ownership survives machine destruction");
        r::example::counter restored;
        check(!restored.restore(snapshot[0]) && restored.value() == 42,
              "owning snapshot restores another machine");
    }
    void cluster(const r::string& executable) {
        directory dir;
        const auto base = std::to_string(available_base());
        const auto result = run({executable, dir.path.native(), "--cycles", "1", "--interval",
                                 "3000", "--base-port", base, "--apply-interval", "20",
                                 "--snapshot-threshold", "4", "--snapshot-trailing", "2"},
                                dir.path / "cluster.log", 25000);
        check(result.status == 0 && result.signal == 0,
              "cluster exits successfully after bounded restart cycle");
        check(result.output.find("cluster restarted server") != r::string::npos,
              "actual server process restarted");
        const std::regex record{
            "server ([123]) stopped count=([0-9]+) restored=([0-9]+) snapshots=([0-9]+)"};
        r::array<bool, 3> stopped{};
        unsigned records = 0, restored = 0, snapshots = 0;
        for (auto it = std::sregex_iterator{result.output.begin(), result.output.end(), record};
             it != std::sregex_iterator{}; ++it) {
            const auto& match = *it;
            const auto index = std::stoul(match[1].str()) - 1;
            check(std::stoull(match[2].str()) > 0, "counter commands committed before shutdown");
            restored += static_cast<unsigned>(std::stoul(match[3].str()));
            snapshots += static_cast<unsigned>(std::stoul(match[4].str()));
            stopped[index] = true;
            ++records;
        }
        if (records != 4 || !restored || !snapshots)
            std::cerr << result.output;
        check(records == 4 && stopped[0] && stopped[1] && stopped[2],
              "one interrupted server and every final server drained");
        check(restored > 0 && snapshots > 0,
              "real snapshots survive process replacement and reach FSM restore");
    }
    void failure(const r::string& executable, const r::string& server) {
        directory dir;
        check(run({server, "--help"}, dir.path / "help.log").status == 0, "server help");
        check(
            run({server, (dir.path / "invalid").native(), "4"}, dir.path / "invalid.log").status !=
                0,
            "invalid node ID rejected");
        const auto base = available_base();
        check(run({server, (dir.path / "bounded").native(), "1", "--base-port",
                   std::to_string(base), "--run-for", "80"},
                  dir.path / "bounded.log")
                      .status == 0,
              "bounded standalone follower closes cleanly");
        check(run({executable, (dir.path / "spawn").native(), "--server",
                   (dir.path / "missing-executable").native()},
                  dir.path / "spawn.log")
                      .status != 0,
              "failed native spawn closes partially initialized process handle");
        socket occupied;
        check(occupied.bind(base + 2) && !::listen(occupied.fd, 1), "occupy second server port");
        const auto result = run({executable, (dir.path / "partial").native(), "--base-port",
                                 std::to_string(base), "--interval", "1000"},
                                dir.path / "partial.log");
        check(result.status != 0 && result.output.find("unexpected server exit") != r::string::npos,
              "failed second server startup stops and reaps the other children");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 4, "suite, cluster and server executable arguments");
        const r::string_view suite{argv[1]};
        if (suite == "counter")
            counter();
        else if (suite == "cluster")
            cluster(argv[2]);
        else if (suite == "failure")
            failure(argv[2], argv[3]);
        else
            check(false, "unknown example suite");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
