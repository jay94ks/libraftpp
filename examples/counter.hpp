#ifndef RAFTPP_EXAMPLE_COUNTER_HPP
#define RAFTPP_EXAMPLE_COUNTER_HPP

// Counter state machine from cowsql/raft example/server.c; see LICENSE.md.
#include <raftpp/state_machine.hpp>
#include <cstring>

namespace raftpp::example {
    class counter final : public state_machine {
    public:
        counter() = default;
        counter(const counter&) = delete;
        counter& operator=(const counter&) = delete;
        error_code apply(view<const byte> command, shared<void>& result) override {
            if (command.size() != sizeof(uint64_t))
                return errc::malformed;
            uint64_t delta;
            std::memcpy(&delta, command.data(), sizeof(delta));
            *value_ += delta;
            result = value_;
            return {};
        }
        error_code snapshot(vector<shared_buffer>& output) override {
            buffer bytes{sizeof(uint64_t)};
            std::memcpy(bytes.data(), value_.get(), sizeof(uint64_t));
            output.emplace_back(std::move(bytes));
            ++snapshots_;
            return {};
        }
        error_code restore(shared_buffer bytes) override {
            if (bytes.size() != sizeof(uint64_t))
                return errc::malformed;
            std::memcpy(value_.get(), bytes.bytes().data(), sizeof(uint64_t));
            ++restores_;
            return {};
        }
        uint64_t value() const noexcept {
            return *value_;
        }
        unsigned restores() const noexcept {
            return restores_;
        }
        unsigned snapshots() const noexcept {
            return snapshots_;
        }

    private:
        shared<uint64_t> value_ = std::make_shared<uint64_t>(0);
        unsigned restores_ = 0, snapshots_ = 0;
    };
}
#endif // RAFTPP_EXAMPLE_COUNTER_HPP
