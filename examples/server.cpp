// C++ port of cowsql/raft example/server.c; see LICENSE.md.
#include "counter.hpp"
#include "runtime.hpp"
#include <raftpp/node.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#include <raftpp/uv/uv_io.hpp>
#include <csignal>

namespace r = raftpp;
namespace ex = raftpp::example;
namespace {
    class server {
    public:
        server(r::fs::path directory, unsigned id, ex::server_settings settings, unsigned duration)
            : directory_(std::move(directory)), id_(id), settings_(settings), duration_(duration),
              on_transfer_([this] { close_node(); }), on_closed_([this] {
                  std::printf("server %u stopped count=%llu restored=%u snapshots=%u\n", id_,
                              static_cast<unsigned long long>(machine_->value()),
                              machine_->restores(), machine_->snapshots());
                  std::fflush(stdout);
              }) {}
        ~server() {
            stop(false);
            (void)uv_run(&loop_.value, UV_RUN_DEFAULT);
        }
        server(const server&) = delete;
        server& operator=(const server&) = delete;
        int run() {
            ex::ensure_directory(directory_);
            network_ = std::make_shared<r::tcp_transport>(loop_.value);
            backend_ = std::make_shared<r::uv_io>(loop_.value, directory_, network_);
            machine_ = std::make_shared<ex::counter>();
            r::node_options options;
            options.pre_vote = true;
            options.snapshot_threshold = settings_.snapshot_threshold;
            options.snapshot_trailing = settings_.snapshot_trailing;
            node_ = std::make_unique<r::node>(id_, ex::address(settings_, id_), backend_, machine_,
                                              options);
            r::configuration members;
            for (unsigned id = 1; id <= 3; ++id) {
                if (auto status = members.add(id, ex::address(settings_, id), r::role::voter))
                    throw r::system_error(status);
            }
            if (auto status = node_->bootstrap(members);
                status && status != r::errc::cannot_bootstrap)
                throw r::system_error(status, r::string{node_->error_details().text()});
            if (auto status = node_->start())
                throw r::system_error(status, r::string{node_->error_details().text()});
            for (unsigned i = 0; i < signals_.size(); ++i) {
                ex::check_uv(uv_signal_init(&loop_.value, &signals_[i]), "uv_signal_init");
                signal_live_[i] = true;
                signals_[i].data = this;
                ex::check_uv(uv_signal_start(
                                 &signals_[i],
                                 [](uv_signal_t* native, int) noexcept {
                                     static_cast<server*>(native->data)->stop(true);
                                 },
                                 i ? SIGTERM : SIGINT),
                             "uv_signal_start");
            }
            ex::check_uv(uv_timer_init(&loop_.value, &apply_timer_), "uv_timer_init");
            apply_live_ = true;
            apply_timer_.data = this;
            ex::check_uv(uv_timer_start(
                             &apply_timer_,
                             [](uv_timer_t* native) noexcept {
                                 static_cast<server*>(native->data)->apply();
                             },
                             0, settings_.apply_interval),
                         "uv_timer_start");
            if (duration_) {
                ex::check_uv(uv_timer_init(&loop_.value, &limit_timer_), "uv_timer_init");
                limit_live_ = true;
                limit_timer_.data = this;
                ex::check_uv(uv_timer_start(
                                 &limit_timer_,
                                 [](uv_timer_t* native) noexcept {
                                     static_cast<server*>(native->data)->stop(true);
                                 },
                                 duration_, 0),
                             "uv_timer_start");
            }
            std::printf("server %u started at %s\n", id_, ex::address(settings_, id_).c_str());
            std::fflush(stdout);
            (void)uv_run(&loop_.value, UV_RUN_DEFAULT);
            return failed_ ? 1 : 0;
        }

    private:
        ex::event_loop loop_;
        r::fs::path directory_;
        unsigned id_;
        ex::server_settings settings_;
        unsigned duration_;
        r::shared<r::tcp_transport> network_;
        r::shared<r::uv_io> backend_;
        r::shared<ex::counter> machine_;
        r::owned<r::node> node_;
        r::close_callback on_transfer_, on_closed_;
        uv_timer_t apply_timer_{}, limit_timer_{}, handoff_timer_{};
        r::array<uv_signal_t, 2> signals_{};
        r::array<bool, 2> signal_live_{};
        bool apply_live_ = false, limit_live_ = false, handoff_live_ = false;
        bool stopping_ = false, node_closing_ = false, failed_ = false;
        void close_node() noexcept {
            if (node_closing_)
                return;
            node_closing_ = true;
            ex::close_handle(handoff_timer_, handoff_live_);
            if (node_) {
                if (node_->close(std::move(on_closed_)))
                    failed_ = true;
            } else if (backend_)
                backend_->close({});
        }
        void stop(bool graceful) noexcept {
            if (stopping_)
                return;
            stopping_ = true;
            ex::close_handle(apply_timer_, apply_live_);
            ex::close_handle(limit_timer_, limit_live_);
            for (unsigned i = 0; i < signals_.size(); ++i)
                ex::close_handle(signals_[i], signal_live_[i]);
            if (graceful && node_ && node_->state() == r::node_state::leader) {
                // An isolated former leader can wait indefinitely to observe a
                // new leader. Bound the application's handoff, then close/drain
                // the request through node::close rather than abandoning it.
                if (!uv_timer_init(&loop_.value, &handoff_timer_)) {
                    handoff_live_ = true;
                    handoff_timer_.data = this;
                    if (!uv_timer_start(
                            &handoff_timer_,
                            [](uv_timer_t* native) noexcept {
                                static_cast<server*>(native->data)->close_node();
                            },
                            2000, 0) &&
                        !node_->transfer(0, std::move(on_transfer_)))
                        return;
                }
            }
            close_node();
        }
        void apply() noexcept {
            if (stopping_)
                return;
            // Bootstrap can report cannot_bootstrap on an ordinary restart.
            // Recorded diagnostics are not themselves a shutdown notification.
            if (node_->state() == r::node_state::unavailable) {
                const auto info = node_->error_details();
                std::fprintf(stderr, "server %u: %.*s\n", id_, static_cast<int>(info.text().size()),
                             info.text().data());
                failed_ = true;
                stop(false);
                return;
            }
            if (node_->state() != r::node_state::leader)
                return;
            try {
                r::buffer command{sizeof(r::uint64_t)};
                const r::uint64_t delta = 1;
                std::memcpy(command.data(), &delta, sizeof(delta));
                const auto status =
                    node_->apply(std::move(command), [this](r::error_code status,
                                                            r::shared<void> result) noexcept {
                        if (status)
                            return; // Leadership changes do not withdraw accepted commands.
                        const auto count = std::static_pointer_cast<r::uint64_t>(result);
                        if (count && *count % 100 == 0) {
                            std::printf("server %u count=%llu\n", id_,
                                        static_cast<unsigned long long>(*count));
                            std::fflush(stdout);
                        }
                    });
                if (status && status != r::errc::not_leader && status != r::errc::no_space)
                    std::fprintf(stderr, "server %u: apply failed (%d)\n", id_, status.value());
            } catch (const std::exception& error) {
                std::fprintf(stderr, "server %u: %s\n", id_, error.what());
                failed_ = true;
                stop(false);
            }
        }
    };
    void usage() {
        std::puts("usage: raftpp-example-server <dir> <id:1..3> [options]\n"
                  "  --base-port N          Ports are N+1, N+2, N+3 (default 9000)\n"
                  "  --apply-interval MS    Counter proposal interval (default 125)\n"
                  "  --snapshot-threshold N (default 64)\n"
                  "  --snapshot-trailing N  (default 16)\n"
                  "  --run-for MS           Stop after MS; default runs until SIGINT/SIGTERM");
    }
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && r::string_view{argv[1]} == "--help") {
            usage();
            return 0;
        }
        if (argc < 3) {
            usage();
            return 1;
        }
        const auto id = ex::number(argv[2], 3, 1);
        ex::server_settings settings;
        unsigned duration = 0;
        for (int i = 3; i < argc; i += 2) {
            if (i + 1 == argc)
                throw r::invalid_argument("missing option value");
            const r::string_view name{argv[i]}, value{argv[i + 1]};
            if (name == "--run-for")
                duration = ex::number(value, r::numeric_limits<unsigned>::max(), 1);
            else if (!ex::setting(settings, name, value))
                throw r::invalid_argument("unknown option: " + r::string{name});
        }
        std::signal(SIGPIPE, SIG_IGN);
        server value{argv[1], id, settings, duration};
        return value.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "server: %s\n", error.what());
        return 1;
    }
}
