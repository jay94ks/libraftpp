#ifndef RAFTPP_BENCHMARK_DEVICE_HPP
#define RAFTPP_BENCHMARK_DEVICE_HPP

// C++ port of cowsql/raft tools/benchmark/fs.c device discovery; see LICENSE.md.
#include <raftpp/common.hpp>
#include <sys/types.h>

namespace raftpp::benchmark {
    enum class device_driver { generic, nvme, null_block };
    struct device_info {
        device_driver driver = device_driver::generic;
        dev_t device = 0;
        dev_t parent_device = 0;
        string disk_name;
        uint64_t logical_block_size = 0;
        uint64_t first_sector = 0, end_sector = 0;
        bool raw = false, write_through = false;
    };
    device_info inspect_device(const fs::path& path, const fs::path& sysfs = "/sys/dev/block");
}
#endif // RAFTPP_BENCHMARK_DEVICE_HPP
