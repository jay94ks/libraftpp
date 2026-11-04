#include <raftpp/composed_io.hpp>
#include <raftpp/random.hpp>
#include "byte.hpp"
#include "snapshot_format.hpp"
#include <algorithm>

namespace raftpp::detail {
    namespace {
        template <typename F> error_code guarded(F&& function) noexcept {
            try {
                return function();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::io_error;
            }
        }
    }
    struct composed_io_state : enable_shared_from_this<composed_io_state> {
        io_components parts;
        shared<composed_io_state> closing_owner;
        shared<function<void()>> tick;
        shared<function<void(message)>> receive;
        close_callback on_close;
        array<close_callback, 3> closed_callbacks;
        error_info diagnostic;
        size_t active = 0, pending = 0, closing_parts = 0;
        uint32_t random_state;
        bool initialized = false, loaded = false, started = false;
        bool initializing = false, loading = false, starting = false;
        bool closing = false, closed = false;
        explicit composed_io_state(io_components value)
            : parts(std::move(value)), random_state(parts.random_seed) {
            if (!parts.storage || !parts.timer || !parts.network)
                throw invalid_argument("I/O requires storage, timer and network");
            for (auto& callback : closed_callbacks)
                callback = [this] {
                    --closing_parts;
                    finish_close();
                };
        }
        struct scope {
            shared<composed_io_state> owner;
            explicit scope(shared<composed_io_state> value) : owner(std::move(value)) {
                ++owner->active;
            }
            ~scope() {
                --owner->active;
                owner->finish_close();
            }
        };
        error_code remember(error_code code, string_view operation, error_info info = {}) noexcept {
            if (code)
                diagnostic = info.code == code ? wrap_error_info(info, operation)
                                               : make_error_info(code, operation);
            return code;
        }
        error_code available() const noexcept {
            return closing        ? make_error_code(errc::shutdown)
                   : !initialized ? make_error_code(errc::invalid)
                                  : error_code{};
        }
        void finish_close() noexcept {
            if (!closing || closed || active || pending || closing_parts)
                return;
            auto keep = shared_from_this();
            closed = true;
            auto callback = std::move(on_close);
            closing_owner.reset();
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
        }
        void begin_close() noexcept {
            if (closing)
                return;
            closing = true;
            started = false;
            closing_owner = shared_from_this();
            scope guard{closing_owner};
            tick.reset();
            receive.reset();
            closing_parts = 3;
            parts.timer->close(std::move(closed_callbacks[0]));
            parts.network->close(std::move(closed_callbacks[1]));
            parts.storage->close(std::move(closed_callbacks[2]));
        }
        error_code payload_size(const snapshot& value, size_t limit, size_t& size) const noexcept {
            size = 0;
            for (const auto& bytes : value.buffers) {
                if (bytes.size() > limit - size)
                    return errc::too_big;
                size += bytes.size();
            }
            return {};
        }
        error_code decode(snapshot& value) noexcept {
            return guarded([&]() -> error_code {
                size_t size = 0;
                const auto limit =
                    std::max(parts.max_snapshot_size, parts.max_encoded_snapshot_size);
                if (auto error = payload_size(value, limit, size))
                    return error;
                array<byte, 8> prefix{};
                size_t count = 0;
                for (const auto& bytes : value.buffers) {
                    auto n = std::min(prefix.size() - count, bytes.size());
                    if (n)
                        std::copy_n(bytes.bytes().begin(), n, prefix.begin() + count);
                    count += n;
                    if (count == prefix.size())
                        break;
                }
                if (count != prefix.size() || prefix != snapshot_envelope_magic)
                    return size <= parts.max_snapshot_size ? error_code{}
                                                           : make_error_code(errc::too_big);
                if (size > parts.max_encoded_snapshot_size)
                    return errc::too_big;
                if (size < 24)
                    return errc::corrupt;
                snapshot flat;
                if (auto error = copy_snapshot(value, flat))
                    return error;
                auto input = flat.buffers.front().bytes();
                const auto encoding = load_little_endian<uint64_t>(input.subspan<8, 8>());
                const auto expected = load_little_endian<uint64_t>(input.subspan<16, 8>());
                if (encoding > 1)
                    return errc::corrupt;
                if (encoding == 1 && !parts.snapshot_codec)
                    return errc::invalid;
                if (expected > parts.max_snapshot_size)
                    return errc::too_big;
                buffer output;
                if (encoding == 0) {
                    if (input.size() - 24 != expected)
                        return errc::corrupt;
                    output = buffer::copy(input.subspan(24));
                } else if (auto error = parts.snapshot_codec->decompress(
                               input.subspan(24), output, static_cast<size_t>(expected)))
                    return error;
                if (output.size() != expected)
                    return errc::corrupt;
                flat.buffers.clear();
                flat.buffers.emplace_back(std::move(output));
                value = std::move(flat);
                return {};
            });
        }
        error_code encode(const shared<const snapshot>& value,
                          shared<const snapshot>& output) noexcept {
            return guarded([&]() -> error_code {
                if (!value)
                    return errc::invalid;
                size_t size;
                if (auto error = payload_size(*value, parts.max_snapshot_size, size))
                    return error;
                array<byte, 8> prefix{};
                size_t count = 0;
                for (const auto& bytes : value->buffers) {
                    const auto n = std::min(prefix.size() - count, bytes.size());
                    if (n)
                        std::copy_n(bytes.bytes().begin(), n, prefix.begin() + count);
                    count += n;
                    if (count == prefix.size())
                        break;
                }
                if (!parts.snapshot_codec &&
                    (count != prefix.size() || prefix != snapshot_envelope_magic)) {
                    output = value;
                    return {};
                }
                auto result = std::make_shared<snapshot>();
                if (auto error = copy_snapshot(*value, *result))
                    return error;
                buffer encoded;
                if (parts.snapshot_codec) {
                    if (auto error = parts.snapshot_codec->compress(result->buffers.front().bytes(),
                                                                    encoded))
                        return error;
                } else
                    encoded = buffer::copy(result->buffers.front().bytes());
                if (parts.max_encoded_snapshot_size < 24 ||
                    encoded.size() > parts.max_encoded_snapshot_size - 24)
                    return errc::too_big;
                buffer envelope{24 + encoded.size()};
                std::copy(snapshot_envelope_magic.begin(), snapshot_envelope_magic.end(),
                          envelope.bytes().begin());
                store_little_endian<uint64_t>(parts.snapshot_codec ? 1 : 0,
                                              envelope.bytes().subspan<8, 8>());
                store_little_endian<uint64_t>(size, envelope.bytes().subspan<16, 8>());
                if (!encoded.empty())
                    std::copy(encoded.bytes().begin(), encoded.bytes().end(),
                              envelope.bytes().begin() + 24);
                result->buffers.clear();
                result->buffers.emplace_back(std::move(envelope));
                output = std::move(result);
                return {};
            });
        }
        struct operation {
            shared<composed_io_state> owner;
            array<shared<const void>, 2> data;
            completion callback;
            bool submitted = false, done = false;
        };
        template <typename Source, typename F>
        error_code submit(Source& source, shared<const void> data, shared<const void> extra,
                          completion callback, F&& invoke) noexcept {
            shared<operation> request;
            error_info submission;
            auto status = guarded([&]() -> error_code {
                request = std::make_shared<operation>();
                request->owner = shared_from_this();
                request->data = {std::move(data), std::move(extra)};
                request->callback = std::move(callback);
                // Construct the completion before increasing the pending count.
                completion done = [request, source = &source](error_code error) {
                    if (std::exchange(request->done, true))
                        return;
                    auto owner = request->owner;
                    scope guard{owner};
                    if (error)
                        owner->remember(error, "io.complete", source->error_details());
                    auto callback = std::move(request->callback);
                    try {
                        if (callback)
                            callback(error);
                    } catch (...) {
                        owner->remember(errc::io_error, "io.callback");
                        owner->begin_close();
                    }
                    callback = {};
                    request->data = {};
                    request->owner.reset();
                    --owner->pending;
                };
                ++pending;
                request->submitted = true;
                auto status = invoke(std::move(done));
                if (status)
                    submission = source.error_details();
                return status;
            });
            if (status) {
                if (request && !request->done) {
                    // A completion may not have been allocated/submitted yet.
                    if (request->owner) {
                        if (request->submitted)
                            --pending;
                        request->done = true;
                        request->callback = {};
                        request->data = {};
                        request->owner.reset();
                    }
                }
                remember(status, "io.submit", submission);
            }
            return status;
        }
    };

    class composed_io final : public io {
        shared<composed_io_state> state_;
        template <typename F> error_code storage_call(string_view name, F&& action) noexcept {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            error_info failure;
            auto error = guarded([&] {
                auto status = action();
                if (status)
                    failure = state->parts.storage->error_details();
                return status;
            });
            if (error)
                state->remember(error, name, failure);
            return !error && state->closing ? make_error_code(errc::shutdown) : error;
        }

    public:
        explicit composed_io(io_components value)
            : state_(std::make_shared<composed_io_state>(std::move(value))) {}
        ~composed_io() override {
            state_->begin_close();
        }
        error_code init(server_id id, string_view address) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (state->closing)
                return errc::shutdown;
            if (!id)
                return errc::bad_id;
            if (address.empty())
                return errc::invalid;
            if (state->initializing || state->initialized)
                return errc::busy;
            state->initializing = true;
            state->diagnostic = {};
            auto error = guarded([&]() -> error_code {
                if (auto e = state->parts.storage->init(id, address))
                    return state->remember(e, "io.storage.init",
                                           state->parts.storage->error_details());
                if (state->closing)
                    return errc::shutdown;
                if (auto e = state->parts.network->init(id, address))
                    return state->remember(e, "io.network.init",
                                           state->parts.network->error_details());
                return state->closing ? make_error_code(errc::shutdown) : error_code{};
            });
            state->initializing = false;
            if (error) {
                state->remember(error, "io.init", state->diagnostic);
                state->begin_close();
            } else
                state->initialized = true;
            return error;
        }
        error_code bootstrap(const configuration& value) noexcept override {
            return storage_call("io.bootstrap",
                                [&] { return state_->parts.storage->bootstrap(value); });
        }
        error_code recover(const configuration& value) noexcept override {
            return storage_call("io.recover",
                                [&] { return state_->parts.storage->recover(value); });
        }
        error_code load(io_state& output) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            if (state->loading || state->loaded)
                return errc::busy;
            state->loading = true;
            state->diagnostic = {};
            auto error = guarded([&]() -> error_code {
                io_state value;
                if (auto e = state->parts.storage->load(value))
                    return state->remember(e, "io.storage.load",
                                           state->parts.storage->error_details());
                if (state->closing)
                    return errc::shutdown;
                if (value.snapshot)
                    if (auto e = state->decode(*value.snapshot))
                        return e;
                if (state->closing)
                    return errc::shutdown;
                output = std::move(value);
                state->loaded = true;
                return {};
            });
            state->loading = false;
            if (error) {
                state->remember(error, "io.load", state->diagnostic);
                state->begin_close();
            }
            return error;
        }
        error_code start(unsigned interval, function<void()> tick,
                         function<void(message)> receive) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            if (!state->loaded || !interval || !tick || !receive)
                return errc::invalid;
            if (state->starting || state->started)
                return errc::busy;
            state->starting = true;
            state->diagnostic = {};
            auto error = guarded([&]() -> error_code {
                state->tick = std::make_shared<function<void()>>(std::move(tick));
                state->receive = std::make_shared<function<void(message)>>(std::move(receive));
                weak<composed_io_state> weak = state;
                function<void(message)> received = [weak](message value) {
                    auto owner = weak.lock();
                    if (!owner || owner->closing)
                        return;
                    composed_io_state::scope guard{owner};
                    auto callback = owner->receive;
                    try {
                        if (callback)
                            (*callback)(std::move(value));
                    } catch (...) {
                        owner->remember(errc::io_error, "io.receive");
                        owner->begin_close();
                    }
                };
                completion ticked = [weak](error_code error) {
                    auto owner = weak.lock();
                    if (!owner || owner->closing)
                        return;
                    composed_io_state::scope guard{owner};
                    if (error) {
                        owner->remember(error, "io.timer", owner->parts.timer->error_details());
                        owner->begin_close();
                        return;
                    }
                    auto callback = owner->tick;
                    try {
                        if (callback)
                            (*callback)();
                    } catch (...) {
                        owner->remember(errc::io_error, "io.tick");
                        owner->begin_close();
                    }
                };
                state->started = true;
                if (auto e = state->parts.network->start(std::move(received)))
                    return state->remember(e, "io.network.start",
                                           state->parts.network->error_details());
                if (state->closing)
                    return errc::shutdown;
                if (auto e = state->parts.timer->start(interval, std::move(ticked)))
                    return state->remember(e, "io.timer.start",
                                           state->parts.timer->error_details());
                return state->closing ? make_error_code(errc::shutdown) : error_code{};
            });
            state->starting = false;
            if (error) {
                state->remember(error, "io.start", state->diagnostic);
                state->begin_close();
            }
            return error;
        }
        void close(close_callback callback) noexcept override {
            auto state = state_;
            if (state->closed) {
                try {
                    if (callback)
                        callback();
                } catch (...) {
                }
                return;
            }
            if (callback && !state->on_close)
                state->on_close = std::move(callback);
            state->begin_close();
        }
        error_code set_term(term value) noexcept override {
            return storage_call("io.set_term",
                                [&] { return state_->parts.storage->set_term(value); });
        }
        error_code set_vote(server_id value) noexcept override {
            return storage_call("io.set_vote",
                                [&] { return state_->parts.storage->set_vote(value); });
        }
        error_code truncate(log_index value) noexcept override {
            return storage_call("io.truncate",
                                [&] { return state_->parts.storage->truncate(value); });
        }
        error_code append(log_index index, shared<const entry_batch> value,
                          completion callback) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            if (!state->loaded || !value)
                return errc::invalid;
            return state->submit(
                *state->parts.storage, value, {}, std::move(callback), [&](completion done) {
                    return state->parts.storage->append(index, value, std::move(done));
                });
        }
        error_code send(shared<const message> value, completion callback) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            if (!state->started || !value)
                return errc::invalid;
            return state->submit(*state->parts.network, value, {}, std::move(callback),
                                 [&](completion done) {
                                     return state->parts.network->send(value, std::move(done));
                                 });
        }
        error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                completion callback) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            if (!state->loaded)
                return errc::invalid;
            shared<const snapshot> encoded;
            if (auto error = state->encode(value, encoded))
                return state->remember(error, "io.snapshot.encode");
            if (state->closing)
                return errc::shutdown;
            return state->submit(
                *state->parts.storage, value, encoded, std::move(callback), [&](completion done) {
                    return state->parts.storage->snapshot_put(trailing, encoded, std::move(done));
                });
        }
        error_code snapshot_get(snapshot_callback callback) noexcept override {
            auto state = state_;
            composed_io_state::scope guard{state};
            if (auto error = state->available())
                return error;
            if (!state->loaded)
                return errc::invalid;
            struct operation {
                shared<composed_io_state> owner;
                snapshot_callback callback;
                bool submitted = false, done = false;
            };
            shared<operation> request;
            error_info submission;
            auto status = guarded([&]() -> error_code {
                request = std::make_shared<operation>();
                request->owner = state;
                request->callback = std::move(callback);
                snapshot_callback done = [request](error_code error, optional<snapshot> value) {
                    if (std::exchange(request->done, true))
                        return;
                    auto owner = request->owner;
                    composed_io_state::scope guard{owner};
                    if (error)
                        owner->remember(error, "io.snapshot.get",
                                        owner->parts.storage->error_details());
                    else if (value) {
                        error = owner->decode(*value);
                        if (error)
                            owner->remember(error, "io.snapshot.decode");
                    }
                    if (error)
                        value.reset();
                    auto callback = std::move(request->callback);
                    try {
                        if (callback)
                            callback(error, std::move(value));
                    } catch (...) {
                        owner->remember(errc::io_error, "io.snapshot.callback");
                        owner->begin_close();
                    }
                    callback = {};
                    value.reset();
                    request->owner.reset();
                    --owner->pending;
                };
                ++state->pending;
                request->submitted = true;
                auto status = state->parts.storage->snapshot_get(std::move(done));
                if (status)
                    submission = state->parts.storage->error_details();
                return status;
            });
            if (status) {
                if (request && !request->done) {
                    if (request->submitted)
                        --state->pending;
                    request->done = true;
                    request->callback = {};
                    request->owner.reset();
                }
                state->remember(status, "io.snapshot.get.submit", submission);
            }
            return status;
        }
        uint64_t time() const noexcept override {
            return state_->parts.timer->time();
        }
        uint32_t random(uint32_t minimum, uint32_t maximum) noexcept override {
            return minimum >= maximum
                       ? minimum
                       : random_within_range(state_->random_state, minimum, maximum - 1);
        }
        uint16_t capacity() const noexcept override {
            return state_->closing ? 0 : state_->parts.storage->capacity();
        }
        error_info error_details() const noexcept override {
            return state_->diagnostic;
        }
    };
}
namespace raftpp {
    shared<io> make_io(io_components components) {
        return std::make_shared<detail::composed_io>(std::move(components));
    }
}
