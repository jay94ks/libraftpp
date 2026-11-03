#include <raftpp/raft.hpp>

int main() {
    raftpp::configuration configuration;
    return configuration.add(1, "core-consumer", raftpp::role::voter) ? 1 : 0;
}
