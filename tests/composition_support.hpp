#ifndef RAFTPP_TEST_COMPOSITION_SUPPORT_HPP
#define RAFTPP_TEST_COMPOSITION_SUPPORT_HPP
#include <raftpp/composed_io.hpp>
#include <raftpp/encoding.hpp>
#include <raftpp/node.hpp>
#include <algorithm>
#include <deque>
#include <iostream>
#include <stdexcept>

namespace composition_testing {
    using namespace raftpp;
    inline void check(bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    }
    inline shared_buffer bytes(string_view text) {
        return shared_buffer::copy({reinterpret_cast<const byte*>(text.data()), text.size()});
    }
    inline string text(const snapshot& value) {
        string result;
        for (const auto& bytes : value.buffers)
            result.append(reinterpret_cast<const char*>(bytes.bytes().data()), bytes.size());
        return result;
    }
    class memory_storage : public storage {
    public:
        io_state persisted;
        std::deque<completion> pending;
        close_callback on_close;
        error_info diagnostic;
        error_code init_failure, submit_failure;
        function<void()> during_init, during_load;
        bool hold = false, closing = false, closed = false, throw_submit = false;
        error_code init(server_id, string_view) override {
            if (during_init)
                during_init();
            return init_failure;
        }
        error_code bootstrap(const configuration& value) override {
            if (persisted.term)
                return errc::cannot_bootstrap;
            buffer encoded;
            if (auto error = value.encode(encoded))
                return error;
            persisted.term = 1;
            persisted.entries.push_back(
                entry{1, entry_type::change, shared_buffer{std::move(encoded)}});
            return {};
        }
        error_code recover(const configuration& value) override {
            buffer encoded;
            if (auto error = value.encode(encoded))
                return error;
            persisted.entries.push_back(
                entry{persisted.term, entry_type::change, shared_buffer{std::move(encoded)}});
            return {};
        }
        error_code load(io_state& output) override {
            if (during_load)
                during_load();
            output = persisted;
            return {};
        }
        error_code set_term(term value) override {
            persisted.term = value;
            persisted.voted_for = 0;
            return {};
        }
        error_code set_vote(server_id value) override {
            persisted.voted_for = value;
            return {};
        }
        error_code truncate(log_index index) override {
            if (index < persisted.start_index ||
                index > persisted.start_index + persisted.entries.size())
                return errc::invalid;
            persisted.entries.resize(static_cast<size_t>(index - persisted.start_index));
            return {};
        }
        error_code append(log_index index, shared<const entry_batch> value,
                          completion callback) override {
            if (throw_submit)
                throw std::runtime_error("submit");
            if (submit_failure)
                return submit_failure;
            if (hold) {
                pending.push_back(std::move(callback));
                return {};
            }
            if (index != persisted.start_index + persisted.entries.size())
                return errc::invalid;
            for (const auto& item : *value)
                persisted.entries.push_back(*item);
            if (callback)
                callback({});
            return {};
        }
        error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                completion callback) override {
            if (submit_failure)
                return submit_failure;
            persisted.snapshot = *value;
            const auto next = value->index + 1;
            const auto retained = next > trailing ? next - trailing : 1;
            if (retained > persisted.start_index) {
                const auto n =
                    std::min<size_t>(persisted.entries.size(), retained - persisted.start_index);
                persisted.entries.erase(persisted.entries.begin(), persisted.entries.begin() + n);
                persisted.start_index += n;
            }
            if (callback)
                callback({});
            return {};
        }
        error_code snapshot_get(snapshot_callback callback) override {
            if (throw_submit)
                throw std::runtime_error("get");
            if (submit_failure)
                return submit_failure;
            if (callback)
                callback({}, persisted.snapshot);
            return {};
        }
        void close(close_callback callback) noexcept override {
            closing = true;
            if (callback && !on_close)
                on_close = std::move(callback);
            finish();
        }
        void finish() noexcept {
            if (!closing || !pending.empty() || closed)
                return;
            closed = true;
            auto callback = std::move(on_close);
            if (callback)
                callback();
        }
        void drain(error_code error = {}) {
            while (!pending.empty()) {
                auto callback = std::move(pending.front());
                pending.pop_front();
                if (callback)
                    callback(error);
            }
            finish();
        }
        uint16_t capacity() const noexcept override {
            return 8192;
        }
        error_info error_details() const noexcept override {
            return diagnostic;
        }
    };
    class manual_timer final : public timer {
    public:
        completion tick;
        error_code start_failure;
        uint64_t now = 0;
        bool closed = false;
        error_code start(unsigned, completion callback) override {
            tick = std::move(callback);
            return start_failure;
        }
        void close(close_callback callback) noexcept override {
            closed = true;
            tick = {};
            if (callback)
                callback();
        }
        uint64_t time() const noexcept override {
            return now;
        }
        void advance(unsigned msecs = 100) {
            now += msecs;
            auto callback = tick;
            if (callback)
                callback({});
        }
    };
    class controlled_transport final : public transport {
    public:
        function<void(message)> reader;
        function<void()> during_start;
        completion writer;
        close_callback on_close;
        error_code init_failure, start_failure, submit_failure;
        bool closed = false;
        error_code init(server_id, string_view) override {
            return init_failure;
        }
        error_code start(function<void(message)> callback) override {
            reader = std::move(callback);
            if (during_start)
                during_start();
            return start_failure;
        }
        error_code send(shared<const message>, completion callback) override {
            if (submit_failure)
                return submit_failure;
            writer = std::move(callback);
            return {};
        }
        void close(close_callback callback) noexcept override {
            reader = {};
            on_close = std::move(callback);
            if (!writer)
                finish();
        }
        void finish() {
            closed = true;
            auto callback = std::move(on_close);
            if (callback)
                callback();
        }
        void drain() {
            auto callback = std::move(writer);
            if (callback)
                callback(errc::canceled);
            finish();
        }
    };
    class machine final : public state_machine {
    public:
        string value;
        error_code apply(view<const byte> command, shared<void>& result) override {
            value.append(reinterpret_cast<const char*>(command.data()), command.size());
            result = std::make_shared<string>(value);
            return {};
        }
        error_code restore(shared_buffer bytes) override {
            value.assign(reinterpret_cast<const char*>(bytes.bytes().data()), bytes.size());
            return {};
        }
        error_code snapshot(vector<shared_buffer>& output) override {
            output.push_back(bytes(value));
            return {};
        }
    };
    inline configuration members() {
        configuration value;
        check(!value.add(1, "local", role::voter), "configuration");
        return value;
    }
}
#endif
