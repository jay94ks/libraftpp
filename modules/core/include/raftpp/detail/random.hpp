#ifndef RAFTPP_DETAIL_RANDOM_HPP
#define RAFTPP_DETAIL_RANDOM_HPP

#include <raftpp/common.hpp>
#include <raftpp/export.hpp>

namespace raftpp::detail {

    // Preserve the reference's seeded sequence rather than changing engines.
    // A singleton range returns its value without advancing state.
    [[nodiscard]] RAFTPP_API uint32_t random_within_range(uint32_t& state, uint32_t minimum,
                                                          uint32_t maximum) noexcept;

}

#endif // RAFTPP_DETAIL_RANDOM_HPP
