// C++ port of cowsql/raft src/random.c; see LICENSE.md.
#include <raftpp/detail/random.hpp>

#include "assert.hpp"

namespace raftpp::detail {

    namespace {

        uint32_t advance(uint32_t& state) noexcept {
            auto result = ((state >> ((state >> 28) + 4)) ^ state) * uint32_t{277803737};
            result ^= result >> 22;
            state = state * uint32_t{747796405} + uint32_t{1729};
            return result;
        }

    }

    uint32_t random_within_range(uint32_t& state, uint32_t minimum, uint32_t maximum) noexcept {
        RAFTPP_ASSERT(minimum <= maximum);
        auto range = maximum - minimum;
        // The C helper divides by zero for a singleton range. Return its only
        // possible value without drawing, rather than reproducing that UB.
        if (range == 0) {
            return minimum;
        }
        if (range == numeric_limits<uint32_t>::max()) {
            return advance(state);
        }
        // Preserve the reference's rejection threshold and seeded output.
        const auto threshold = (~range + uint32_t{1}) % range;
        ++range;
        uint32_t result;
        do {
            result = advance(state);
        } while (result < threshold);
        return minimum + result % range;
    }

}
