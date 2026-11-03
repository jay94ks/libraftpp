#ifndef RAFTPP_RANDOM_HPP
#define RAFTPP_RANDOM_HPP
#include <raftpp/detail/random.hpp>
namespace raftpp {
    // Preserve the C library's seeded generator and inclusive bounds.
    using detail::random_within_range;
}
#endif // RAFTPP_RANDOM_HPP
