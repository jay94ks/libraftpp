#ifndef RAFTPP_TEST_TCP_FAILURE_HPP
#define RAFTPP_TEST_TCP_FAILURE_HPP
namespace tcp_failure {
    enum class operation {
        none,
        tcp_init,
        listen,
        resolve,
        connect,
        write,
        accept,
        read,
        timer_init,
        write_result
    };
    void after(operation operation, unsigned skip, int status) noexcept;
}
#endif
