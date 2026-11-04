#include "composition_support.hpp"
#include <raftpp/framed_transport.hpp>

namespace allocation_failure {
    void after(std::ptrdiff_t);
}
namespace {
    using namespace composition_testing;
    class controlled_codec final : public compression_codec {
    public:
        mutable unsigned encoded = 0, decoded = 0;
        error_code failure;
        error_code compress(view<const byte> input, buffer& output) const noexcept override {
            ++encoded;
            if (failure)
                return failure;
            try {
                buffer copy{input.size() + 1};
                copy.bytes()[0] = byte{0x35};
                std::reverse_copy(input.begin(), input.end(), copy.bytes().begin() + 1);
                output.swap(copy);
                return {};
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            }
        }
        error_code decompress(view<const byte> input, buffer& output,
                              size_t limit) const noexcept override {
            ++decoded;
            if (failure)
                return failure;
            if (input.empty() || input[0] != byte{0x35})
                return errc::corrupt;
            if (input.size() - 1 > limit)
                return errc::too_big;
            try {
                buffer copy{input.size() - 1};
                std::reverse_copy(input.begin() + 1, input.end(), copy.bytes().begin());
                output.swap(copy);
                return {};
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            }
        }
    };
    struct setup {
        shared<memory_storage> disk = std::make_shared<memory_storage>();
        shared<manual_timer> clock = std::make_shared<manual_timer>();
        shared<controlled_transport> network = std::make_shared<controlled_transport>();
        shared<io> backend;
        explicit setup(shared<compression_codec> codec = {})
            : backend(make_io({.storage = disk,
                               .timer = clock,
                               .network = network,
                               .snapshot_codec = std::move(codec)})) {}
        void start() {
            io_state state;
            check(!backend->init(1, "local") && !backend->load(state) &&
                      !backend->start(
                          100, [] {}, [](message) {}),
                  "start composed I/O");
        }
    };
    shared<snapshot> snapshot_value(string_view text) {
        auto value = std::make_shared<snapshot>();
        value->term = 1;
        value->index = 4;
        value->configuration_index = 1;
        value->configuration = members();
        value->buffers.push_back(bytes(text));
        return value;
    }
    void contracts() {
        bool rejected = false;
        try {
            (void)make_io({});
        } catch (const invalid_argument&) {
            rejected = true;
        }
        check(rejected, "missing components");
        setup unit;
        unit.start();
        check(unit.backend->capacity() == 8192, "capacity delegated");
        check(unit.backend->init(1, "local") == errc::busy, "duplicate init");
        check(!unit.backend->set_term(7) && !unit.backend->set_vote(2) &&
                  unit.disk->persisted.term == 7 && unit.disk->persisted.voted_for == 2,
              "metadata");
        auto batch = std::make_shared<entry_batch>();
        weak<entry_batch> weak_batch = batch;
        unit.disk->hold = true;
        bool completed = false, closed = false;
        check(!unit.backend->append(1, batch,
                                    [&](error_code error) {
                                        check(error == errc::canceled && !weak_batch.expired() &&
                                                  !closed,
                                              "accepted request lifetime");
                                        completed = true;
                                    }),
              "append");
        batch.reset();
        auto value = std::make_shared<message>();
        weak<message> weak_message = value;
        check(!unit.backend->send(value,
                                  [&](error_code error) {
                                      check(error == errc::canceled && !weak_message.expired() &&
                                                !closed,
                                            "send lifetime");
                                  }),
              "send");
        value.reset();
        unit.backend->close([&] {
            check(completed && weak_batch.expired() && weak_message.expired(),
                  "release before close");
            closed = true;
        });
        unit.backend.reset();
        check(!closed, "asynchronous close");
        unit.network->drain();
        check(!closed, "wait for storage");
        unit.disk->drain(errc::canceled);
        check(closed, "closed after requests");
    }
    void failures() {
        setup unit;
        unit.start();
        auto batch = std::make_shared<entry_batch>();
        unsigned calls = 0;
        unit.disk->submit_failure = errc::no_space;
        unit.disk->diagnostic = make_error_info(
            errc::no_space, "disk", {}, std::make_error_code(std::errc::no_space_on_device));
        check(unit.backend->append(1, batch, [&](error_code) { ++calls; }) == errc::no_space &&
                  calls == 0,
              "immediate failure");
        check(unit.backend->error_details().cause == unit.disk->diagnostic.cause, "owning cause");
        unit.disk->throw_submit = true;
        unit.disk->diagnostic = make_error_info(errc::io_error, "stale", {},
                                                std::make_error_code(std::errc::permission_denied));
        check(unit.backend->append(1, batch, {}) == errc::io_error &&
                  !unit.backend->error_details().cause,
              "throw does not reuse stale diagnostics");
        check(unit.backend->snapshot_get({}) == errc::io_error &&
                  !unit.backend->error_details().cause,
              "get throw");
        unit.backend->close({});
        for (unsigned step = 0; step < 3; ++step) {
            setup failed;
            if (step == 0)
                failed.disk->init_failure = errc::io_error;
            if (step == 1)
                failed.network->init_failure = errc::no_connection;
            if (step < 2)
                check(failed.backend->init(1, "local") != error_code{}, "init failure");
            else {
                check(!failed.backend->init(1, "local"), "init");
                io_state value;
                check(!failed.backend->load(value), "load");
                failed.clock->start_failure = errc::io_error;
                check(failed.backend->start(
                          1, [] {}, [](message) {}) == errc::io_error,
                      "timer failure");
            }
            check(failed.disk->closed && failed.clock->closed && failed.network->closed,
                  "failure closes all components");
        }
    }
    void reentrancy() {
        for (unsigned point = 0; point < 3; ++point) {
            setup unit;
            bool closed = false;
            auto close = [&] { unit.backend->close([&] { closed = true; }); };
            if (point == 0) {
                unit.disk->during_init = close;
                check(unit.backend->init(1, "local") == errc::shutdown, "close during init");
            } else {
                check(!unit.backend->init(1, "local"), "init");
                if (point == 1)
                    unit.disk->during_load = close;
                io_state value;
                value.term = 42;
                auto error = unit.backend->load(value);
                if (point == 1)
                    check(error == errc::shutdown && value.term == 42,
                          "close during load preserves output");
                else {
                    check(!error, "load");
                    unit.network->during_start = close;
                    check(unit.backend->start(
                              1, [] {}, [](message) {}) == errc::shutdown,
                          "close during network start");
                }
            }
            check(closed, "reentrant close finishes after call");
        }
        setup unit;
        unit.start();
        bool inside = false, closed = false;
        check(unit.backend->start(
                  1, [] {}, [](message) {}) == errc::busy,
              "duplicate start rejected");
        unit.backend->close([&] { closed = true; });
        check(closed && !inside, "close");
        setup tick;
        io_state state;
        check(!tick.backend->init(1, "local") && !tick.backend->load(state), "init tick");
        check(!tick.backend->start(
                  1,
                  [&] {
                      inside = true;
                      tick.backend->close([&] {
                          check(!inside, "tick callback before close");
                          closed = true;
                      });
                      tick.backend.reset();
                      inside = false;
                  },
                  [](message) {}),
              "tick start");
        closed = false;
        tick.clock->advance();
        check(closed, "tick shutdown");
    }
    void snapshots() {
        auto codec = std::make_shared<controlled_codec>();
        setup unit{codec};
        unit.start();
        auto value = snapshot_value("payload");
        unsigned calls = 0;
        check(!unit.backend->snapshot_put(1, value,
                                          [&](error_code error) {
                                              check(!error, "snapshot put");
                                              ++calls;
                                          }),
              "put accepted");
        check(codec->encoded == 1 && text(*unit.disk->persisted.snapshot) != "payload",
              "encoded storage payload");
        check(!unit.backend->snapshot_get([&](error_code error, optional<snapshot> output) {
            check(!error && output && text(*output) == "payload" && output->index == 4,
                  "snapshot decoded");
            ++calls;
        }) && calls == 2,
              "snapshot get");
        auto envelope = *unit.disk->persisted.snapshot;
        auto saved = unit.disk->persisted;
        unit.backend->close({});
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            setup reader{scenario == 0 ? shared<compression_codec>{} : codec};
            reader.disk->persisted = saved;
            auto raw = buffer::copy(envelope.buffers.front().bytes());
            if (scenario == 1)
                raw.bytes()[8] = byte{2}; // Unknown encoding.
            if (scenario == 2)
                raw.bytes()[16] = byte{0xff}; // Decoded size exceeds actual output.
            if (scenario == 3)
                raw.resize(12); // Truncated header.
            reader.disk->persisted.snapshot->buffers = {shared_buffer{std::move(raw)}};
            check(!reader.backend->init(1, "local"), "reader init");
            io_state output;
            output.term = 77;
            check(reader.backend->load(output) != error_code{} && output.term == 77,
                  "bad envelope preserves output");
        }
        setup raw;
        raw.start();
        string reserved = "RAFTPP";
        reserved.push_back(1);
        reserved.push_back(0);
        reserved += "raw";
        check(!raw.backend->snapshot_put(0, snapshot_value(reserved), {}),
              "escape reserved prefix without codec");
        check(!raw.backend->snapshot_get([&](error_code error, optional<snapshot> value) {
            check(!error && value && text(*value) == reserved, "escaped roundtrip");
        }),
              "get escaped");
        raw.backend->close({});
    }
    void limits() {
        class zero_frame_codec final : public compression_codec {
        public:
            error_code compress(view<const byte> input, buffer& output) const noexcept override {
                if (input.size() != 1 || input[0] != byte{'Z'})
                    return errc::invalid;
                output.clear();
                return {};
            }
            error_code decompress(view<const byte> input, buffer& output,
                                  size_t maximum) const noexcept override {
                if (!input.empty())
                    return errc::corrupt;
                if (maximum < 1)
                    return errc::too_big;
                try {
                    buffer value{1};
                    value.bytes()[0] = byte{'Z'};
                    output.swap(value);
                    return {};
                } catch (const bad_alloc&) {
                    return errc::out_of_memory;
                }
            }
        };
        setup zero{std::make_shared<zero_frame_codec>()};
        zero.start();
        check(!zero.backend->snapshot_put(0, snapshot_value("Z"), {}), "empty encoded frame");
        check(!zero.backend->snapshot_get([](error_code error, optional<snapshot> value) {
            check(!error && value && text(*value) == "Z", "zero frame decoding");
        }),
              "empty frame get");
        zero.backend->close({});
        auto disk = std::make_shared<memory_storage>();
        auto clock = std::make_shared<manual_timer>();
        auto network = std::make_shared<controlled_transport>();
        auto codec = std::make_shared<controlled_codec>();
        auto backend = make_io({.storage = disk,
                                .timer = clock,
                                .network = network,
                                .snapshot_codec = codec,
                                .max_snapshot_size = 8,
                                .max_encoded_snapshot_size = 30});
        io_state output;
        check(!backend->init(1, "local") && !backend->load(output), "bounded backend");
        unsigned callbacks = 0;
        check(backend->snapshot_put(0, snapshot_value("too-large"),
                                    [&](error_code) { ++callbacks; }) == errc::too_big,
              "raw size bound");
        check(backend->snapshot_put(0, snapshot_value("payload"),
                                    [&](error_code) { ++callbacks; }) == errc::too_big,
              "encoded size bound includes header");
        check(!callbacks && !disk->persisted.snapshot, "bounds preserve stored snapshot");
        backend->close({});
        // A length declaration is checked before asking a codec to allocate.
        setup encoded{codec};
        encoded.start();
        check(!encoded.backend->snapshot_put(0, snapshot_value("small"), {}),
              "encode bounded input");
        auto saved = encoded.disk->persisted;
        encoded.backend->close({});
        auto raw = buffer::copy(saved.snapshot->buffers.front().bytes());
        for (size_t i = 16; i < 24; ++i)
            raw.bytes()[i] = byte{0xff};
        saved.snapshot->buffers = {shared_buffer{std::move(raw)}};
        setup reader{codec};
        reader.disk->persisted = std::move(saved);
        check(!reader.backend->init(1, "local"), "bounded reader init");
        output.term = 99;
        const auto calls = codec->decoded;
        check(reader.backend->load(output) == errc::too_big && output.term == 99 &&
                  codec->decoded == calls,
              "declared output bound before decode");
    }
    void allocations() {
        setup unit;
        unit.start();
        auto batch = std::make_shared<entry_batch>();
        unsigned failures = 0, calls = 0;
        for (std::ptrdiff_t n = 0; n < 8; ++n) {
            allocation_failure::after(n);
            auto status = unit.backend->append(1, batch, [&](error_code error) {
                check(!error, "append completion");
                ++calls;
            });
            allocation_failure::after(-1);
            if (status) {
                check(status == errc::out_of_memory, "allocation mapping");
                ++failures;
            }
        }
        check(failures >= 2 && calls > 0, "allocation points reached");
        bool closed = false;
        unit.backend->close([&] { closed = true; });
        check(closed, "no pending request leak after OOM");
    }
    void node_integration() {
        auto disk = std::make_shared<memory_storage>();
        auto clock = std::make_shared<manual_timer>();
        auto network = std::make_shared<framed_transport>();
        auto codec = std::make_shared<controlled_codec>();
        auto backend =
            make_io({.storage = disk, .timer = clock, .network = network, .snapshot_codec = codec});
        auto fsm = std::make_shared<machine>();
        node_options options;
        options.tracing = tracer::noop();
        options.snapshot_threshold = 1;
        options.snapshot_trailing = 0;
        node member{1, "local", backend, fsm, options};
        check(!member.bootstrap(members()) && !member.start() &&
                  member.state() == node_state::leader,
              "composed node leader");
        unsigned applied = 0;
        check(!member.apply(bytes("command"),
                            [&](error_code error, shared<void> result) {
                                check(!error &&
                                          *std::static_pointer_cast<string>(result) == "command",
                                      "node applied");
                                ++applied;
                            }) &&
                  applied == 1,
              "node apply");
        bool closed = false;
        check(!member.close([&] { closed = true; }), "node close");
        check(closed, "node drained");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite required");
        string_view suite = argv[1];
        if (suite == "contracts")
            contracts();
        else if (suite == "failures")
            failures();
        else if (suite == "reentrancy")
            reentrancy();
        else if (suite == "snapshots")
            snapshots();
        else if (suite == "limits")
            limits();
        else if (suite == "allocations")
            allocations();
        else if (suite == "node")
            node_integration();
        else
            check(false, "unknown suite");
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
