#ifndef RAFTPP_ERROR_HPP
#define RAFTPP_ERROR_HPP

#include <raftpp/common.hpp>
#include <raftpp/export.hpp>
#include <type_traits>

namespace raftpp {

    // Numeric values match the original Raft status codes.
    enum class errc {
        success = 0,
        out_of_memory,
        bad_id,
        duplicate_id,
        duplicate_address,
        bad_role,
        malformed,
        not_leader,
        leadership_lost,
        shutdown,
        cannot_bootstrap,
        cannot_change,
        corrupt,
        canceled,
        name_too_long,
        too_big,
        no_connection,
        busy,
        io_error,
        not_found,
        invalid,
        unauthorized,
        no_space,
        too_many
    };

    RAFTPP_API const error_category& raft_category() noexcept;
    RAFTPP_API error_code make_error_code(errc value) noexcept;
    [[nodiscard]] RAFTPP_API string_view error_message(errc value) noexcept;

    // Domain diagnostics own standard values and a bounded legacy-sized message.
    // Copies remain valid after callbacks, backend replacement, and shutdown.
    struct error_info {
        error_code code;
        error_code cause;
        array<char, 256> message{};
        [[nodiscard]] RAFTPP_API string_view text() const noexcept;
    };
    [[nodiscard]] RAFTPP_API error_info make_error_info(error_code code, string_view operation = {},
                                                        string_view resource = {},
                                                        error_code cause = {}) noexcept;
    [[nodiscard]] RAFTPP_API error_info wrap_error_info(error_info info,
                                                        string_view prefix) noexcept;

}

namespace std {
    template <> struct is_error_code_enum<raftpp::errc> : true_type {};
}

#endif // RAFTPP_ERROR_HPP
