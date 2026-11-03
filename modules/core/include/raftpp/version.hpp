#ifndef RAFTPP_VERSION_HPP
#define RAFTPP_VERSION_HPP
#include <raftpp/common.hpp>
namespace raftpp {
    // Version of the vendored behavioral reference, not a C++ ABI version.
    inline constexpr array<unsigned, 3> reference_version{0, 22, 1};
    inline constexpr unsigned reference_version_number =
        reference_version[0] * 10000 + reference_version[1] * 100 + reference_version[2];
}
#endif // RAFTPP_VERSION_HPP
