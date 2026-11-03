#include <raftpp/config.hpp>
#include <raftpp/detail/random.hpp>

#include <iostream>
#include <string_view>

#if defined(__unix__)
#include <sys/resource.h>
#endif

int main(int argc, char** argv) {
    static_assert(raftpp::features::backtrace == (RAFTPP_WITH_BACKTRACE != 0));
    if (argc == 2 && std::string_view{argv[1]} == "--info") {
#if defined(NDEBUG)
        constexpr bool active = false;
#else
        constexpr bool active = true;
#endif
        std::cout << "active=" << active << " backtrace=" << raftpp::features::backtrace << '\n';
        return 0;
    }
    if (argc != 1)
        return 2;
#if defined(__unix__)
    // An expected child abort must not leave core files in the caller's tree.
    const rlimit limit{0, 0};
    if (setrlimit(RLIMIT_CORE, &limit) != 0) {
        std::cerr << "cannot disable core dumps\n";
        return 2;
    }
#endif
    // Exercise an assertion in the actual library, including across a DSO.
    // With NDEBUG the uint32_t difference is UINT32_MAX and takes the helper's
    // explicitly supported full-range branch, avoiding division by zero/UB.
    raftpp::uint32_t state = 1;
    (void)raftpp::detail::random_within_range(state, 2, 1);
    return 0;
}
