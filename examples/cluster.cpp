// C++ port of cowsql/raft example/cluster.c; see LICENSE.md.
#include "runtime.hpp"
#include <csignal>
#include <random>

namespace r = raftpp;
namespace ex = raftpp::example;
namespace {
    r::fs::path server_executable() {
        r::string path(1024, '\0');
        for (;;) {
            auto size = path.size();
            const auto status = uv_exepath(path.data(), &size);
            if (!status) {
                path.resize(size);
                return r::fs::path{path}.parent_path() / "raftpp-example-server";
            }
            if (status != UV_ENOBUFS)
                ex::check_uv(status, "uv_exepath");
            path.resize(path.size() * 2);
        }
    }
    class cluster {
        struct child {
            r::borrowed<cluster> owner = nullptr;
            uv_process_t process{};
            bool live = false, running = false, planned = false;
        };
        enum class phase { select, restart, settle };

    public:
        cluster(r::fs::path directory, ex::server_settings settings, unsigned cycles,
                unsigned interval, r::fs::path executable)
            : directory_(std::move(directory)), executable_(std::move(executable)),
              settings_(settings), cycle_limit_(cycles), fixed_interval_(interval),
              random_(std::random_device{}()) {
            for (auto& child : children_)
                child.owner = this;
        }
        ~cluster() {
            stop();
            (void)uv_run(&loop_.value, UV_RUN_DEFAULT);
        }
        cluster(const cluster&) = delete;
        cluster& operator=(const cluster&) = delete;
        int run() {
            ex::ensure_directory(directory_);
            ex::check_uv(uv_timer_init(&loop_.value, &timer_), "uv_timer_init");
            timer_live_ = true;
            timer_.data = this;
            ex::check_uv(uv_timer_init(&loop_.value, &grace_), "uv_timer_init");
            grace_live_ = true;
            grace_.data = this;
            for (unsigned i = 0; i < signals_.size(); ++i) {
                ex::check_uv(uv_signal_init(&loop_.value, &signals_[i]), "uv_signal_init");
                signal_live_[i] = true;
                signals_[i].data = this;
                ex::check_uv(uv_signal_start(
                                 &signals_[i],
                                 [](uv_signal_t* native, int) noexcept {
                                     static_cast<cluster*>(native->data)->stop();
                                 },
                                 i ? SIGTERM : SIGINT),
                             "uv_signal_start");
            }
            for (unsigned i = 0; i < children_.size() && !stopping_; ++i)
                spawn(i);
            if (!stopping_)
                schedule(next_interval(), phase::select);
            (void)uv_run(&loop_.value, UV_RUN_DEFAULT);
            return failed_ ? 1 : 0;
        }

    private:
        ex::event_loop loop_;
        r::fs::path directory_, executable_;
        ex::server_settings settings_;
        unsigned cycle_limit_, fixed_interval_, cycles_ = 0, selected_ = 0, pause_ = 0;
        std::mt19937 random_;
        r::array<child, 3> children_{};
        uv_timer_t timer_{}, grace_{};
        r::array<uv_signal_t, 2> signals_{};
        r::array<bool, 2> signal_live_{};
        phase phase_ = phase::select;
        bool timer_live_ = false, grace_live_ = false, stopping_ = false, failed_ = false;
        template <typename F> void safely(F&& action) noexcept {
            try {
                action();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "cluster: %s\n", error.what());
                failed_ = true;
                stop();
            }
        }
        unsigned next_interval() {
            return fixed_interval_ ? fixed_interval_
                                   : std::uniform_int_distribution<unsigned>{1, 15}(random_) * 1000;
        }
        void schedule(unsigned interval, phase next) {
            phase_ = next;
            pause_ = interval;
            ex::check_uv(uv_timer_start(
                             &timer_,
                             [](uv_timer_t* native) noexcept {
                                 auto& self = *static_cast<cluster*>(native->data);
                                 self.safely([&] { self.advance(); });
                             },
                             interval, 0),
                         "uv_timer_start");
        }
        void spawn(unsigned index) {
            auto& child = children_[index];
            const auto directory = directory_ / std::to_string(index + 1);
            ex::ensure_directory(directory);
            r::vector<r::string> storage{executable_.native(),
                                         directory.native(),
                                         std::to_string(index + 1),
                                         "--base-port",
                                         std::to_string(settings_.base_port),
                                         "--apply-interval",
                                         std::to_string(settings_.apply_interval),
                                         "--snapshot-threshold",
                                         std::to_string(settings_.snapshot_threshold),
                                         "--snapshot-trailing",
                                         std::to_string(settings_.snapshot_trailing)};
            r::vector<char*> arguments;
            for (auto& value : storage)
                arguments.push_back(value.data());
            arguments.push_back(nullptr);
            r::array<uv_stdio_container_t, 3> stdio{};
            for (unsigned i = 0; i < stdio.size(); ++i) {
                stdio[i].flags = UV_INHERIT_FD;
                stdio[i].data.fd = static_cast<int>(i);
            }
            uv_process_options_t options{};
            options.file = arguments.front();
            options.args = arguments.data();
            options.stdio_count = static_cast<int>(stdio.size());
            options.stdio = stdio.data();
            options.exit_cb = [](uv_process_t* native, int64_t status, int signal) noexcept {
                auto& child = *static_cast<cluster::child*>(native->data);
                auto& self = *child.owner;
                child.running = false;
                uv_close(reinterpret_cast<uv_handle_t*>(native), [](uv_handle_t* native) noexcept {
                    auto& child = *static_cast<cluster::child*>(native->data);
                    auto& self = *child.owner;
                    child.live = false;
                    self.safely([&] {
                        if (self.stopping_)
                            self.maybe_finish();
                        else if (child.planned)
                            self.schedule(self.pause_, phase::restart);
                    });
                });
                if (!self.stopping_ &&
                    (!child.planned || status != 0 || (signal != 0 && signal != SIGINT))) {
                    std::fprintf(stderr,
                                 "cluster: unexpected server exit (status=%lld signal=%d)\n",
                                 static_cast<long long>(status), signal);
                    self.failed_ = true;
                    self.stop();
                }
            };
            child.process = {};
            child.planned = false;
            const auto status = uv_spawn(&loop_.value, &child.process, &options);
            child.process.data = &child;
            if (status) {
                if (child.process.type == UV_PROCESS) {
                    child.live = true;
                    uv_close(reinterpret_cast<uv_handle_t*>(&child.process),
                             [](uv_handle_t* native) noexcept {
                                 auto& child = *static_cast<cluster::child*>(native->data);
                                 child.live = false;
                                 child.owner->maybe_finish();
                             });
                }
                std::fprintf(stderr, "cluster: spawn: %s\n", uv_strerror(status));
                failed_ = true;
                stop();
                return;
            }
            child.live = child.running = true;
            std::printf("cluster started server %u\n", index + 1);
            std::fflush(stdout);
        }
        void advance() {
            if (stopping_)
                return;
            if (phase_ == phase::settle) {
                stop();
                return;
            }
            if (phase_ == phase::restart) {
                spawn(selected_);
                if (stopping_)
                    return;
                ++cycles_;
                std::printf("cluster restarted server %u cycle=%u\n", selected_ + 1, cycles_);
                std::fflush(stdout);
                schedule(next_interval(),
                         cycle_limit_ && cycles_ >= cycle_limit_ ? phase::settle : phase::select);
                return;
            }
            selected_ = std::uniform_int_distribution<unsigned>{0, 2}(random_);
            auto& child = children_[selected_];
            child.planned = true;
            std::printf("cluster stopping server %u\n", selected_ + 1);
            std::fflush(stdout);
            ex::check_uv(uv_process_kill(&child.process, SIGINT), "uv_process_kill");
        }
        void maybe_finish() noexcept {
            if (!stopping_)
                return;
            for (const auto& child : children_)
                if (child.live)
                    return;
            ex::close_handle(grace_, grace_live_);
        }
        void stop() noexcept {
            if (stopping_)
                return;
            stopping_ = true;
            ex::close_handle(timer_, timer_live_);
            for (unsigned i = 0; i < signals_.size(); ++i)
                ex::close_handle(signals_[i], signal_live_[i]);
            bool running = false;
            for (auto& child : children_)
                if (child.running) {
                    running = true;
                    const auto status = uv_process_kill(&child.process, SIGINT);
                    if (status && status != UV_ESRCH) {
                        std::fprintf(stderr, "cluster: stop: %s\n", uv_strerror(status));
                        failed_ = true;
                    }
                }
            if (running && grace_live_) {
                const auto status = uv_timer_start(
                    &grace_,
                    [](uv_timer_t* native) noexcept {
                        auto& self = *static_cast<cluster*>(native->data);
                        for (auto& child : self.children_)
                            if (child.running) {
                                self.failed_ = true;
                                (void)uv_process_kill(&child.process, SIGKILL);
                            }
                        ex::close_handle(self.grace_, self.grace_live_);
                    },
                    5000, 0);
                if (status) {
                    failed_ = true;
                    for (auto& child : children_)
                        if (child.running)
                            (void)uv_process_kill(&child.process, SIGKILL);
                    ex::close_handle(grace_, grace_live_);
                }
            }
            maybe_finish();
        }
    };
    void usage() {
        std::puts(
            "usage: raftpp-example-cluster [dir] [options]\n"
            "  --cycles N             Stop after N outages/restarts (default runs until signal)\n"
            "  --interval MS          Fixed outage/downtime/settle interval (default random 1..15 s)\n"
            "  --server PATH          Server executable (default sibling raftpp-example-server)\n"
            "  --base-port N          (default 9000)\n"
            "  --apply-interval MS    (default 125)\n"
            "  --snapshot-threshold N (default 64)\n"
            "  --snapshot-trailing N  (default 16)");
    }
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && r::string_view{argv[1]} == "--help") {
            usage();
            return 0;
        }
        r::fs::path directory = "/tmp/raftpp";
        int first = 1;
        if (argc > 1 && !r::string_view{argv[1]}.starts_with("--")) {
            directory = argv[1];
            first = 2;
        }
        ex::server_settings settings;
        unsigned cycles = 0, interval = 0;
        r::fs::path executable;
        for (int i = first; i < argc; i += 2) {
            if (i + 1 == argc)
                throw r::invalid_argument("missing option value");
            const r::string_view name{argv[i]}, value{argv[i + 1]};
            if (name == "--cycles")
                cycles = ex::number(value, r::numeric_limits<unsigned>::max(), 1);
            else if (name == "--interval")
                interval = ex::number(value, r::numeric_limits<unsigned>::max(), 1);
            else if (name == "--server")
                executable = value;
            else if (!ex::setting(settings, name, value))
                throw r::invalid_argument("unknown option: " + r::string{name});
        }
        if (executable.empty())
            executable = server_executable();
        std::signal(SIGPIPE, SIG_IGN);
        cluster value{directory, settings, cycles, interval, executable};
        return value.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "cluster: %s\n", error.what());
        return 1;
    }
}
