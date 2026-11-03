#include "uring.hpp"
#if RAFTPP_BENCHMARK_HAS_IO_URING
#include <cerrno>
#include <bit>
#include <fcntl.h>
#include <linux/fs.h>
#include <unistd.h>

namespace raftpp::benchmark {
    int uring_operations::setup(unsigned entries, io_uring_params* params) noexcept {
        return static_cast<int>(syscall(SYS_io_uring_setup, entries, params));
    }
    int uring_operations::register_resources(int fd, unsigned op, const void* data,
                                             unsigned count) noexcept {
        return static_cast<int>(syscall(SYS_io_uring_register, fd, op, data, count));
    }
    int uring_operations::enter(int fd, unsigned submit, unsigned complete,
                                unsigned flags) noexcept {
        return static_cast<int>(
            syscall(SYS_io_uring_enter, fd, submit, complete, flags, nullptr, 0));
    }
    void* uring_operations::map(size_t size, int fd, off_t offset) noexcept {
        return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, offset);
    }
    int uring_operations::unmap(void* address, size_t size) noexcept {
        return munmap(address, size);
    }
    int uring_operations::close(int fd) noexcept {
        return ::close(fd);
    }
    uring_operations& native_uring_operations() noexcept {
        static uring_operations instance;
        return instance;
    }
    namespace {
        static_assert(atomic_ref<unsigned>::is_always_lock_free);
        size_t checked_size(size_t bytes) {
            if (bytes < 512 || bytes > 1024 * 1024 || !std::has_single_bit(bytes))
                throw invalid_argument("invalid io_uring write buffer");
            return bytes;
        }
        unsigned acquire(borrowed<unsigned> value) noexcept {
            return atomic_ref<unsigned>{*value}.load(std::memory_order_acquire);
        }
        void release(borrowed<unsigned> value, unsigned next) noexcept {
            atomic_ref<unsigned>{*value}.store(next, std::memory_order_release);
        }
        [[noreturn]] void fail(string_view operation) {
            const auto cause = errno;
            throw system_error(cause, std::generic_category(), string{operation});
        }
        template <typename T>
        borrowed<T> field(void* base, size_t size, unsigned offset, size_t count = 1) {
            if (offset % alignof(T) || offset > size || count > (size - offset) / sizeof(T))
                throw std::runtime_error("invalid io_uring mapping offsets");
            return reinterpret_cast<T*>(static_cast<byte*>(base) + offset);
        }
        size_t extent(unsigned offset, unsigned count, size_t width) {
            if (count == 0 || count > (numeric_limits<size_t>::max() - offset) / width)
                throw std::runtime_error("invalid io_uring ring size");
            return offset + static_cast<size_t>(count) * width;
        }
    }
    uring_writer::uring_writer(int target, size_t bytes, ref<uring_operations> operations)
        : operations_(operations), buffer_(checked_size(bytes), std::max<size_t>(bytes, 4096)) {
        for (size_t i = 0; i < bytes; ++i)
            buffer_.data()[i] = static_cast<byte>(i % 128);
        try {
            initialize(target);
        } catch (...) {
            (void)close();
            throw;
        }
    }
    void uring_writer::initialize(int target) {
        io_uring_params params{}; // No optional setup flags: works on older kernels.
        auto& api = operations_.get();
        descriptor_ = api.setup(1, &params);
        if (descriptor_ < 0)
            fail("io_uring_setup");
        if (!std::has_single_bit(params.sq_entries) || !std::has_single_bit(params.cq_entries))
            throw std::runtime_error("invalid io_uring entry count");
        submission_size_ = extent(params.sq_off.array, params.sq_entries, sizeof(unsigned));
        completion_size_ = extent(params.cq_off.cqes, params.cq_entries, sizeof(io_uring_cqe));
        const bool shared = params.features & IORING_FEAT_SINGLE_MMAP;
        if (shared)
            submission_size_ = std::max(submission_size_, completion_size_);
        submission_ = api.map(submission_size_, descriptor_, IORING_OFF_SQ_RING);
        if (submission_ == MAP_FAILED)
            fail("mmap io_uring submission ring");
        if (shared)
            completion_ = submission_;
        else {
            completion_ = api.map(completion_size_, descriptor_, IORING_OFF_CQ_RING);
            if (completion_ == MAP_FAILED)
                fail("mmap io_uring completion ring");
        }
        entries_size_ = extent(0, params.sq_entries, sizeof(io_uring_sqe));
        entries_ = api.map(entries_size_, descriptor_, IORING_OFF_SQES);
        if (entries_ == MAP_FAILED)
            fail("mmap io_uring entries");
        const auto cq_size = shared ? submission_size_ : completion_size_;
        submission_head_ = field<unsigned>(submission_, submission_size_, params.sq_off.head);
        submission_tail_ = field<unsigned>(submission_, submission_size_, params.sq_off.tail);
        submission_array_ =
            field<unsigned>(submission_, submission_size_, params.sq_off.array, params.sq_entries);
        submission_mask_ = *field<unsigned>(submission_, submission_size_, params.sq_off.ring_mask);
        completion_head_ = field<unsigned>(completion_, cq_size, params.cq_off.head);
        completion_tail_ = field<unsigned>(completion_, cq_size, params.cq_off.tail);
        completion_mask_ = *field<unsigned>(completion_, cq_size, params.cq_off.ring_mask);
        // Retain the validated CQE offset separately from the mapping base.
        completion_entries_ =
            field<io_uring_cqe>(completion_, cq_size, params.cq_off.cqes, params.cq_entries);
        if (submission_mask_ != params.sq_entries - 1 || completion_mask_ != params.cq_entries - 1)
            throw std::runtime_error("invalid io_uring ring mask");
        if (api.register_resources(descriptor_, IORING_REGISTER_FILES, &target, 1) < 0)
            fail("io_uring register file");
        files_registered_ = true;
        const iovec buffer{buffer_.data(), buffer_.size()};
        if (api.register_resources(descriptor_, IORING_REGISTER_BUFFERS, &buffer, 1) < 0)
            fail("io_uring register buffer");
        buffers_registered_ = true;
    }
    bool uring_writer::consume(int& result, uint64_t& token) noexcept {
        const auto head = acquire(completion_head_);
        if (head == acquire(completion_tail_))
            return false;
        const auto& entry = completion_entries_[head & completion_mask_];
        result = entry.res;
        token = entry.user_data;
        release(completion_head_, head + 1);
        pending_ = false;
        return true;
    }
    void uring_writer::write(uint64_t offset, clock::time_point deadline,
                             borrowed<const volatile std::sig_atomic_t> stop) {
        auto check_deadline = [&] {
            if (stop && *stop)
                throw system_error(std::make_error_code(std::errc::interrupted),
                                   "disk benchmark interrupted");
            if (clock::now() >= deadline)
                throw system_error(std::make_error_code(std::errc::timed_out),
                                   "io_uring completion deadline");
        };
        check_deadline();
        if (descriptor_ < 0 || pending_)
            throw std::logic_error("io_uring writer is closed or busy");
        if (offset > static_cast<uint64_t>(numeric_limits<int64_t>::max()) - buffer_.size())
            throw invalid_argument("io_uring write offset overflow");
        const auto tail = acquire(submission_tail_);
        if (tail != acquire(submission_head_))
            throw std::runtime_error("io_uring queue is not empty");
        const auto index = tail & submission_mask_;
        auto& entry = static_cast<io_uring_sqe*>(entries_)[index];
        entry = {};
        entry.opcode = IORING_OP_WRITE_FIXED;
        entry.flags = IOSQE_FIXED_FILE;
        entry.fd = 0;
        entry.addr = reinterpret_cast<uintptr_t>(buffer_.data());
        entry.len = static_cast<unsigned>(buffer_.size());
        entry.off = offset;
        entry.rw_flags = RWF_DSYNC;
        entry.buf_index = 0;
        entry.user_data = ++token_;
        submission_array_[index] = index;
        published_tail_ = tail + 1;
        pending_ = true;
        release(submission_tail_, published_tail_);
        auto& api = operations_.get();
        for (;;) {
            const auto result = api.enter(descriptor_, 1, 0, IORING_ENTER_GETEVENTS);
            if (result == 1)
                break;
            if (result < 0 && errno == EINTR) {
                if (acquire(submission_head_) == published_tail_)
                    break;
                check_deadline();
                continue;
            }
            if (result < 0)
                fail("io_uring_enter submit");
            throw std::runtime_error("io_uring did not submit the write");
        }
        unsigned polls = 0;
        for (;;) {
            int result = 0;
            uint64_t token = 0;
            if (consume(result, token)) {
                if (token != token_)
                    throw std::runtime_error("unexpected io_uring completion token");
                if (result < -4095)
                    throw std::runtime_error("invalid io_uring completion result");
                if (result < 0)
                    throw system_error(-result, std::generic_category(),
                                       "io_uring write completion");
                if (static_cast<size_t>(result) != buffer_.size())
                    throw system_error(std::make_error_code(std::errc::io_error),
                                       "short io_uring write");
                check_deadline();
                return;
            }
            check_deadline();
            // A zero-minimum enter also services kernels that defer task work.
            // It never changes the benchmark to a blocking/pwrite fallback.
            if (++polls % 1024 == 0 && api.enter(descriptor_, 0, 0, IORING_ENTER_GETEVENTS) < 0 &&
                errno != EINTR)
                fail("io_uring_enter poll");
        }
    }
    void uring_writer::drain() noexcept {
        if (!pending_)
            return;
        auto& api = operations_.get();
        if (acquire(submission_head_) != published_tail_) {
            // No SQPOLL is enabled: an unconsumed failed submission is private.
            release(submission_tail_, published_tail_ - 1);
            pending_ = false;
            return;
        }
        int result = 0;
        uint64_t token = 0;
        while (!consume(result, token)) {
            if (api.enter(descriptor_, 0, 1, IORING_ENTER_GETEVENTS) >= 0 || errno == EINTR)
                continue;
            // UNREGISTER_BUFFERS synchronously releases pinned buffers, unlike
            // close(2), whose ring teardown may run asynchronously in the kernel.
            int status;
            do {
                status = api.register_resources(descriptor_, IORING_UNREGISTER_BUFFERS, nullptr, 0);
            } while (status < 0 && errno == EINTR);
            if (status < 0)
                std::terminate(); // Cannot safely release in-flight memory.
            buffers_registered_ = false;
            pending_ = false;
            break;
        }
    }
    error_code uring_writer::close() noexcept {
        if (descriptor_ < 0)
            return {};
        drain();
        error_code error;
        auto retain = [&](int status) {
            if (status < 0 && !error)
                error = {errno, std::generic_category()};
        };
        auto& api = operations_.get();
        auto unregister = [&](unsigned operation) {
            int status;
            do {
                status = api.register_resources(descriptor_, operation, nullptr, 0);
            } while (status < 0 && errno == EINTR);
            retain(status);
        };
        if (buffers_registered_)
            unregister(IORING_UNREGISTER_BUFFERS);
        if (files_registered_)
            unregister(IORING_UNREGISTER_FILES);
        if (entries_ != MAP_FAILED)
            retain(api.unmap(entries_, entries_size_));
        if (completion_ != MAP_FAILED && completion_ != submission_)
            retain(api.unmap(completion_, completion_size_));
        if (submission_ != MAP_FAILED)
            retain(api.unmap(submission_, submission_size_));
        retain(api.close(descriptor_));
        descriptor_ = -1;
        pending_ = buffers_registered_ = files_registered_ = false;
        return error;
    }
    uring_writer::~uring_writer() {
        (void)close();
    }
}
#endif
