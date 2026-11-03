#ifndef RAFTPP_TEST_FILE_ALLOCATION_HPP
#define RAFTPP_TEST_FILE_ALLOCATION_HPP

#include <stddef.h>
#include <stdint.h>

struct raftpp_allocation_plan {
    long block_size;
    int allocation_error;
    int stat_error;
    int write_after;
    int write_error;
};
struct raftpp_allocation_trace {
    unsigned allocations;
    unsigned queries;
    unsigned writes;
    uint64_t positions[64];
};

#ifdef __cplusplus
extern "C" {
#endif
void raftpp_allocation_begin(const struct raftpp_allocation_plan* plan);
void raftpp_allocation_end(struct raftpp_allocation_trace* trace);
#ifdef __cplusplus
}
#endif

#endif
