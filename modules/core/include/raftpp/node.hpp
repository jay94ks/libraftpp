#ifndef RAFTPP_NODE_HPP
#define RAFTPP_NODE_HPP

#include <raftpp/io.hpp>
#include <raftpp/state_machine.hpp>
#include <raftpp/tracing.hpp>
#include <raftpp/memory.hpp>

namespace raftpp {

    using step_callback = function<void()>;

    namespace detail {
        class node_impl;
        struct fixture_access;
        template <typename... Args, typename F> function<void(Args...)> own_callback(F&& callback) {
            using callback_type = decay_t<F>;
            if constexpr (std::is_pointer_v<callback_type> ||
                          std::is_member_pointer_v<callback_type>) {
                callback_type pointer = callback;
                if (pointer == nullptr)
                    return {};
            }
            auto owner = std::allocate_shared<callback_type>(resource_allocator<callback_type>{},
                                                             std::forward<F>(callback));
            return [owner = std::move(owner)](Args... args) {
                std::invoke(*owner, std::move(args)...);
            };
        }
    }

    struct node_options {
        unsigned election_timeout = 1000;
        unsigned heartbeat_timeout = 100;
        unsigned install_snapshot_timeout = 30000;
        unsigned snapshot_threshold = 1024;
        unsigned snapshot_trailing = 2048;
        unsigned max_catch_up_rounds = 10;
        unsigned max_catch_up_round_duration = 5000;
        unsigned max_inflight_entries = 32;
        uint16_t capacity_threshold = 4096;
        bool pre_vote = false;
        bool force_barrier = false;
        tracer tracing{};
    };

    class RAFTPP_API node {
    public:
        node(server_id id, string address, shared<io> backend, shared<state_machine> fsm,
             node_options options = {});
        ~node();
        node(const node&) = delete;
        node& operator=(const node&) = delete;
        node(node&&) noexcept;
        node& operator=(node&&) noexcept;

        [[nodiscard]] error_code bootstrap(const raftpp::configuration& configuration) noexcept;
        [[nodiscard]] error_code recover(const raftpp::configuration& configuration) noexcept;
        [[nodiscard]] error_code start() noexcept;
        error_code close(close_callback callback = {}) noexcept;
        [[nodiscard]] error_code apply(shared_buffer command,
                                       apply_callback callback = {}) noexcept;
        [[nodiscard]] error_code barrier(completion callback = {}) noexcept;
        [[nodiscard]] error_code add(server_id id, string_view address,
                                     completion callback = {}) noexcept;
        [[nodiscard]] error_code assign(server_id id, role role, completion callback = {}) noexcept;
        [[nodiscard]] error_code remove(server_id id, completion callback = {}) noexcept;
        [[nodiscard]] error_code transfer(server_id id = 0, close_callback callback = {}) noexcept;

        template <typename Storage>
            requires(std::is_same_v<Storage, buffer> || std::is_same_v<Storage, aligned_buffer>)
        error_code apply(Storage command, apply_callback callback = {}) noexcept {
            try {
                return apply(shared_buffer{std::move(command)}, std::move(callback));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            }
        }
        template <typename Storage, typename F>
            requires((std::is_same_v<Storage, buffer> || std::is_same_v<Storage, aligned_buffer>) &&
                     !std::is_same_v<remove_cvref_t<F>, apply_callback> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&, error_code, shared<void>>)
        error_code apply(Storage command, F&& callback) noexcept {
            try {
                return apply(shared_buffer{std::move(command)}, std::forward<F>(callback));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }

        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, apply_callback> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&, error_code, shared<void>>)
        error_code apply(shared_buffer command, F&& callback) noexcept {
            try {
                return apply(std::move(command), detail::own_callback<error_code, shared<void>>(
                                                     std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, completion> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&, error_code>)
        error_code barrier(F&& callback) noexcept {
            try {
                return barrier(detail::own_callback<error_code>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, completion> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&, error_code>)
        error_code add(server_id id, string_view address, F&& callback) noexcept {
            try {
                return add(id, address,
                           detail::own_callback<error_code>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, completion> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&, error_code>)
        error_code assign(server_id id, role role, F&& callback) noexcept {
            try {
                return assign(id, role,
                              detail::own_callback<error_code>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, completion> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&, error_code>)
        error_code remove(server_id id, F&& callback) noexcept {
            try {
                return remove(id, detail::own_callback<error_code>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, close_callback> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&>)
        error_code transfer(server_id id, F&& callback) noexcept {
            try {
                return transfer(id, detail::own_callback<>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, close_callback> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&>)
        error_code close(F&& callback) noexcept {
            try {
                return close(detail::own_callback<>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (...) {
                return errc::invalid;
            }
        }

        [[nodiscard]] error_code set_step_callback(step_callback callback = {}) noexcept;
        template <typename F>
            requires(!std::is_same_v<remove_cvref_t<F>, step_callback> &&
                     std::is_invocable_r_v<void, remove_cvref_t<F>&>)
        [[nodiscard]] error_code set_step_callback(F&& callback) noexcept {
            try {
                return set_step_callback(detail::own_callback<>(std::forward<F>(callback)));
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }

        [[nodiscard]] node_state state() const noexcept;
        [[nodiscard]] term current_term() const noexcept;
        [[nodiscard]] server_id voted_for() const noexcept;
        [[nodiscard]] log_index last_index() const noexcept;
        [[nodiscard]] log_index commit_index() const noexcept;
        [[nodiscard]] log_index last_applied() const noexcept;
        [[nodiscard]] uint64_t timeout() const noexcept;
        [[nodiscard]] optional<role> local_role() const noexcept;
        [[nodiscard]] optional<leader_info> leader() const;
        [[nodiscard]] raftpp::configuration configuration() const;
        [[nodiscard]] error_code match_index(server_id id, log_index& index) const noexcept;
        [[nodiscard]] error_code catch_up(server_id id, catch_up_status& status) const noexcept;
        [[nodiscard]] server_id transferee() const noexcept;
        [[nodiscard]] error_code error() const noexcept;
        [[nodiscard]] string error_message() const;
        [[nodiscard]] raftpp::error_info error_details() const noexcept;
        void seed(uint32_t value) noexcept;
        void set_tracer(tracer value) noexcept;
        [[nodiscard]] error_code set_election_timeout(unsigned value) noexcept;
        [[nodiscard]] error_code set_heartbeat_timeout(unsigned value) noexcept;
        [[nodiscard]] error_code set_install_snapshot_timeout(unsigned value) noexcept;
        void set_pre_vote(bool value) noexcept;
        void set_snapshot_threshold(unsigned value) noexcept;
        void set_snapshot_trailing(unsigned value) noexcept;
        void set_max_catch_up_rounds(unsigned value) noexcept;
        void set_max_catch_up_round_duration(unsigned value) noexcept;
        void set_max_inflight_entries(unsigned value) noexcept;
        void set_capacity_threshold(uint16_t value) noexcept;

    private:
        friend struct detail::fixture_access;
        shared<detail::node_impl> impl_;
    };

}

#endif // RAFTPP_NODE_HPP
