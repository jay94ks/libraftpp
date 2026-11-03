#ifndef RAFTPP_ASSERT_HPP
#define RAFTPP_ASSERT_HPP
#include <raftpp/config.hpp>
#include <cassert>
#if RAFTPP_WITH_BACKTRACE && !defined(NDEBUG)
#include <raftpp/common.hpp>
#include <raftpp/export.hpp>
namespace raftpp::detail {
    [[noreturn]] RAFTPP_API void assertion_failed(string_view expression,
                                                  source_location location) noexcept;
}
#define RAFTPP_ASSERT(expression)                                                                  \
    do {                                                                                           \
        if (!static_cast<bool>(expression))                                                        \
            ::raftpp::detail::assertion_failed(#expression, ::raftpp::source_location::current()); \
    } while (false)
#else
#define RAFTPP_ASSERT(expression) assert(expression)
#endif
#endif // RAFTPP_ASSERT_HPP
