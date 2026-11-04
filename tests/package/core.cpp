#include <raftpp/raft.hpp>

int main() {
    raftpp::configuration configuration;
    if (configuration.add(1, "core-consumer", raftpp::role::voter))
        return 1;
    try {
        (void)raftpp::make_io({});
    } catch (const std::invalid_argument&) {
        return 0;
    }
    return 1;
}
