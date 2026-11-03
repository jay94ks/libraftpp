#ifndef RAFTPP_BENCHMARK_URING_HPP
#define RAFTPP_BENCHMARK_URING_HPP

// C++ port of cowsql/raft tools/benchmark/disk_uring.c; see LICENSE.md.
#include "report.hpp"
#include <raftpp/buffer.hpp>
#include <csignal>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#ifndef RAFTPP_BENCHMARK_HAS_IO_URING
#define RAFTPP_BENCHMARK_HAS_IO_URING 0
#endif
#if RAFTPP_BENCHMARK_HAS_IO_URING
#include <linux/io_uring.h>

namespace raftpp::benchmark {
    // Private syscall boundary. The writer owns its ring/mappings/buffer; the
    // target FD and this adapter are borrowed until close completes.
    struct uring_operations {
        virtual ~uring_operations() = default;
        virtual int setup(unsigned entries, io_uring_params* params) noexcept;
        virtual int register_resources(int fd, unsigned op, const void* data,
                                       unsigned count) noexcept;
        virtual int enter(int fd, unsigned submit, unsigned complete, unsigned flags) noexcept;
        virtual void* map(size_t size, int fd, off_t offset) noexcept;
        virtual int unmap(void* address, size_t size) noexcept;
        virtual int close(int fd) noexcept;
    };
    uring_operations& native_uring_operations() noexcept;
    class uring_writer {
    public:
        uring_writer(int target, size_t bytes,
                     ref<uring_operations> operations = native_uring_operations());
        ~uring_writer();
        uring_writer(const uring_writer&) = delete;
        uring_writer& operator=(const uring_writer&) = delete;
        void write(uint64_t offset, clock::time_point deadline,
                   borrowed<const volatile std::sig_atomic_t> stop = nullptr);
        error_code close() noexcept;

    private:
        void initialize(int target);
        bool consume(int& result, uint64_t& token) noexcept;
        void drain() noexcept;
        ref<uring_operations> operations_;
        aligned_buffer buffer_;
        int descriptor_ = -1;
        void* submission_ = MAP_FAILED;
        void* completion_ = MAP_FAILED;
        void* entries_ = MAP_FAILED;
        size_t submission_size_ = 0, completion_size_ = 0, entries_size_ = 0;
        borrowed<unsigned> submission_head_ = nullptr, submission_tail_ = nullptr,
                           submission_array_ = nullptr;
        borrowed<unsigned> completion_head_ = nullptr, completion_tail_ = nullptr;
        borrowed<io_uring_cqe> completion_entries_ = nullptr;
        unsigned submission_mask_ = 0, completion_mask_ = 0;
        bool files_registered_ = false, buffers_registered_ = false, pending_ = false;
        unsigned published_tail_ = 0;
        uint64_t token_ = 0;
    };
}
#endif
#endif // RAFTPP_BENCHMARK_URING_HPP
