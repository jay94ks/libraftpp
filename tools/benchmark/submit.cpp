// C++ port of cowsql/raft tools/benchmark/submit.c; see LICENSE.md.
#include "submit.hpp"
#include "runtime.hpp"
#include <raftpp/node.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#include <raftpp/uv/uv_io.hpp>
#include <csignal>

namespace raftpp::benchmark {
    namespace {
        class discard_machine final : public state_machine {
        public:
            error_code apply(view<const byte>, shared<void>& result) override {
                result.reset();
                return {};
            }
            error_code snapshot(vector<shared_buffer>&) override {
                return errc::invalid;
            }
            error_code restore(shared_buffer) override {
                return errc::invalid;
            }
        };
        class submit_runner {
        public:
            explicit submit_runner(const options& settings)
                : settings_(settings), directory_(settings.directory),
                  latencies_(static_cast<size_t>(settings.operations())),
                  writes_(static_cast<size_t>(settings.operations())),
                  on_closed_([this] { close_controls(); }) {}
            ~submit_runner() {
                stop();
                (void)uv_run(&loop_.value, UV_RUN_DEFAULT);
            }
            submit_runner(const submit_runner&) = delete;
            submit_runner& operator=(const submit_runner&) = delete;
            report run() {
                tracer observer{[this](const trace_event& event) noexcept { trace(event); }};
                uv_io_options io_options;
                io_options.tracing = observer;
                network_ = std::make_shared<tcp_transport>(loop_.value);
                backend_ =
                    std::make_shared<uv_io>(loop_.value, directory_.path(), network_, io_options);
                machine_ = std::make_shared<discard_machine>();
                node_options node_settings;
                node_settings.snapshot_threshold = numeric_limits<unsigned>::max();
                node_ =
                    std::make_unique<node>(1, settings_.address, backend_, machine_, node_settings);
                configuration members;
                if (auto status = members.add(1, settings_.address, role::voter))
                    throw system_error(status);
                if (auto status = node_->bootstrap(members))
                    throw system_error(status, string{node_->error_details().text()});
                if (auto status = node_->start())
                    throw system_error(status, string{node_->error_details().text()});
                check_uv(uv_timer_init(&loop_.value, &timer_), "uv_timer_init");
                timer_live_ = true;
                timer_.data = this;
                check_uv(uv_timer_init(&loop_.value, &deadline_), "uv_timer_init");
                deadline_live_ = true;
                deadline_.data = this;
                check_uv(uv_timer_start(
                             &deadline_,
                             [](uv_timer_t* native) noexcept {
                                 auto& self = *static_cast<submit_runner*>(native->data);
                                 self.fail("submit benchmark timed out");
                             },
                             settings_.timeout, 0),
                         "uv_timer_start");
                for (unsigned i = 0; i < signals_.size(); ++i) {
                    check_uv(uv_signal_init(&loop_.value, &signals_[i]), "uv_signal_init");
                    signal_live_[i] = true;
                    signals_[i].data = this;
                    check_uv(uv_signal_start(
                                 &signals_[i],
                                 [](uv_signal_t* native, int) noexcept {
                                     static_cast<submit_runner*>(native->data)
                                         ->fail("submit benchmark interrupted");
                                 },
                                 i ? SIGTERM : SIGINT),
                             "uv_signal_start");
                }
                next();
                (void)uv_run(&loop_.value, UV_RUN_DEFAULT);
                if (failed_ || trace_failed_ || pending_write_ ||
                    completed_ != settings_.operations() || latencies_.size() != completed_ ||
                    writes_.size() == 0)
                    throw std::runtime_error(
                        "submit benchmark did not produce a complete measurement");
                report output;
                output.add("submit:" + std::to_string(settings_.buffer), metric_kind::latency,
                           latencies_.summarize());
                output.add("submit:write:" + std::to_string(settings_.buffer), metric_kind::latency,
                           writes_.summarize());
                node_.reset();
                backend_.reset();
                network_.reset();
                machine_.reset();
                directory_.cleanup();
                return output;
            }

        private:
            event_loop loop_;
            options settings_;
            temporary_directory directory_;
            latency_samples latencies_, writes_;
            shared<tcp_transport> network_;
            shared<uv_io> backend_;
            shared<state_machine> machine_;
            owned<node> node_;
            close_callback on_closed_;
            uv_timer_t timer_{}, deadline_{};
            array<uv_signal_t, 2> signals_{};
            array<bool, 2> signal_live_{};
            clock::time_point started_{}, write_started_{};
            uint64_t completed_ = 0;
            bool timer_live_ = false, deadline_live_ = false, stopping_ = false, failed_ = false;
            bool pending_write_ = false, trace_failed_ = false;
            void trace(const trace_event& event) noexcept {
                try {
                    if (event.type == trace_write_submit) {
                        if (pending_write_)
                            trace_failed_ = true;
                        pending_write_ = true;
                        write_started_ = clock::now();
                    } else if (event.type == trace_write_complete) {
                        if (!pending_write_) {
                            trace_failed_ = true;
                            return;
                        }
                        writes_.add(elapsed(write_started_));
                        pending_write_ = false;
                    }
                } catch (...) {
                    trace_failed_ = true;
                }
            }
            void close_controls() noexcept {
                close_handle(timer_, timer_live_);
                close_handle(deadline_, deadline_live_);
                for (unsigned i = 0; i < signals_.size(); ++i)
                    close_handle(signals_[i], signal_live_[i]);
            }
            void stop() noexcept {
                if (stopping_)
                    return;
                stopping_ = true;
                if (node_) {
                    if (node_->close(std::move(on_closed_))) {
                        failed_ = true;
                        (void)node_->close();
                        close_controls();
                    }
                } else {
                    if (backend_)
                        backend_->close({});
                    close_controls();
                }
            }
            void fail(const char* message) noexcept {
                std::fprintf(stderr, "%s\n", message);
                failed_ = true;
                stop();
            }
            void complete(error_code status) noexcept {
                if (stopping_)
                    return;
                if (status || trace_failed_) {
                    fail("submit completion or native trace failed");
                    return;
                }
                try {
                    latencies_.add(elapsed(started_));
                    ++completed_;
                    // Test completion before warmup: even a one-entry run ends.
                    if (completed_ == settings_.operations()) {
                        stop();
                        return;
                    }
                    if (completed_ == 1) {
                        check_uv(uv_timer_start(
                                     &timer_,
                                     [](uv_timer_t* native) noexcept {
                                         auto& self = *static_cast<submit_runner*>(native->data);
                                         try {
                                             if (!fs::exists(self.directory_.path() / "open-2") ||
                                                 !fs::exists(self.directory_.path() / "open-3"))
                                                 return;
                                             (void)uv_timer_stop(native);
                                             self.next();
                                         } catch (...) {
                                             self.fail("prepare-pool warmup failed");
                                         }
                                     },
                                     10, 10),
                                 "uv_timer_start");
                    } else
                        next();
                } catch (...) {
                    fail("submit measurement failed");
                }
            }
            void next() noexcept {
                if (stopping_)
                    return;
                try {
                    started_ = clock::now();
                    const auto overhead = completed_ == 0 ? 40U : 32U;
                    buffer command{settings_.buffer - overhead};
                    if (auto status = node_->apply(
                            std::move(command),
                            [this](error_code status, shared<void>) noexcept { complete(status); }))
                        fail("submit request was rejected");
                } catch (...) {
                    fail("submit buffer/callback allocation failed");
                }
            }
        };
    }
    report submit(const options& options) {
        validate_submit(options);
        submit_runner runner{options};
        return runner.run();
    }
}
