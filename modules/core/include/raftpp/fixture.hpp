#ifndef RAFTPP_FIXTURE_HPP
#define RAFTPP_FIXTURE_HPP

#include <raftpp/config.hpp>
#include <raftpp/node.hpp>

#if RAFTPP_WITH_FIXTURE
namespace raftpp {
    namespace detail {
        struct fixture_state;
    }
    enum class fixture_event_type : uint8_t { tick = 1, network, disk, work };
    enum class fixture_message_type : uint8_t {
        append_entries = 1,
        append_entries_result,
        request_vote,
        request_vote_result,
        install_snapshot,
        timeout_now
    };
    struct fixture_event {
        size_t server_index;
        fixture_event_type type;
        uint64_t time;
    };

    // Single-threaded virtual-time cluster; no native loop or sockets are used.
    class RAFTPP_API fixture {
    public:
        inline static constexpr size_t max_servers = 8;
        fixture();
        ~fixture();
        fixture(const fixture&) = delete;
        fixture& operator=(const fixture&) = delete;
        fixture(fixture&&) noexcept;
        fixture& operator=(fixture&&) noexcept;
        [[nodiscard]] error_code grow(shared<state_machine> machine,
                                      node_options options = {}) noexcept;
        [[nodiscard]] error_code configuration(size_t voters,
                                               raftpp::configuration& output) const noexcept;
        [[nodiscard]] error_code bootstrap(const raftpp::configuration& configuration) noexcept;
        [[nodiscard]] error_code start() noexcept;
        void close() noexcept;
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] uint64_t time() const noexcept;
        [[nodiscard]] size_t leader_index() const noexcept; // size() means no stable leader.
        [[nodiscard]] log_index commit_index() const noexcept;
        [[nodiscard]] error_code error() const noexcept;
        [[nodiscard]] node& get(size_t index);
        [[nodiscard]] const node& get(size_t index) const;
        [[nodiscard]] shared<io> backend(size_t index) const;
        [[nodiscard]] bool alive(size_t index) const;
        [[nodiscard]] server_id voted_for(size_t index) const;
        [[nodiscard]] error_code persisted(size_t index, io_state& output) const noexcept;

        [[nodiscard]] optional<fixture_event> step();
        [[nodiscard]] optional<fixture_event> step_n(size_t count);
        [[nodiscard]] bool step_until(const function<bool(const fixture&)>& stop,
                                      uint64_t max_msecs);
        void step_until_elapsed(uint64_t msecs);
        [[nodiscard]] bool step_until_has_leader(uint64_t max_msecs);
        [[nodiscard]] bool step_until_has_no_leader(uint64_t max_msecs);
        [[nodiscard]] bool step_until_applied(size_t index, log_index entry, uint64_t max_msecs);
        [[nodiscard]] bool step_until_state_is(size_t index, node_state state, uint64_t max_msecs);
        [[nodiscard]] bool step_until_term_is(size_t index, term term, uint64_t max_msecs);
        [[nodiscard]] bool step_until_voted_for(size_t index, size_t candidate, uint64_t max_msecs);
        [[nodiscard]] bool step_until_delivered(size_t from, size_t to, uint64_t max_msecs);
        void hook(function<void(fixture&, const fixture_event&)> callback);
        [[nodiscard]] error_code start_elect(size_t index) noexcept;
        [[nodiscard]] bool elect(size_t index, uint64_t max_msecs = 20000);
        [[nodiscard]] bool depose(uint64_t max_msecs = 3000);

        [[nodiscard]] error_code disconnect(size_t from, size_t to) noexcept;
        [[nodiscard]] error_code reconnect(size_t from, size_t to) noexcept;
        [[nodiscard]] error_code saturate(size_t from, size_t to) noexcept;
        [[nodiscard]] error_code desaturate(size_t from, size_t to) noexcept;
        [[nodiscard]] bool saturated(size_t from, size_t to) const;
        [[nodiscard]] error_code kill(size_t index) noexcept;
        [[nodiscard]] error_code revive(size_t index) noexcept;
        [[nodiscard]] error_code set_randomized_election_timeout(size_t index,
                                                                 unsigned msecs) noexcept;
        [[nodiscard]] error_code set_network_latency(size_t index, unsigned msecs) noexcept;
        [[nodiscard]] error_code set_disk_latency(size_t index, unsigned msecs) noexcept;
        [[nodiscard]] error_code set_term(size_t index, term term) noexcept;
        [[nodiscard]] error_code set_snapshot(size_t index, const snapshot& value) noexcept;
        [[nodiscard]] error_code add_entry(size_t index, const entry& value) noexcept;
        [[nodiscard]] error_code io_fault(size_t index, int delay, int repeat) noexcept;
        [[nodiscard]] uint64_t n_send(size_t index, fixture_message_type type) const;
        [[nodiscard]] uint64_t n_recv(size_t index, fixture_message_type type) const;

    private:
        shared<detail::fixture_state> state_;
    };
}
#endif
#endif // RAFTPP_FIXTURE_HPP
