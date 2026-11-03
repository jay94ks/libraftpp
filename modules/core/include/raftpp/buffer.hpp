#ifndef RAFTPP_BUFFER_HPP
#define RAFTPP_BUFFER_HPP

#include <raftpp/memory.hpp>
#include <raftpp/export.hpp>

namespace raftpp {

    // Owns ordinary byte storage and its allocation resource.
    class buffer {
    public:
        buffer() = default;
        explicit buffer(resource_allocator<byte> allocator) : storage_(std::move(allocator)) {}
        explicit buffer(size_t size, resource_allocator<byte> allocator = {})
            : storage_(size, std::move(allocator)) {}

        buffer(const buffer&) = delete;
        buffer& operator=(const buffer&) = delete;
        buffer(buffer&&) noexcept = default;
        buffer& operator=(buffer&&) noexcept = default;

        static buffer copy(view<const byte> bytes, resource_allocator<byte> allocator = {}) {
            buffer result{std::move(allocator)};
            // An empty span may have null iterators; do not subtract them.
            if (!bytes.empty()) {
                result.storage_.assign(bytes.begin(), bytes.end());
            }
            return result;
        }

        [[nodiscard]] size_t size() const noexcept {
            return storage_.size();
        }
        [[nodiscard]] bool empty() const noexcept {
            return storage_.empty();
        }
        [[nodiscard]] borrowed<byte> data() noexcept {
            return storage_.data();
        }
        [[nodiscard]] borrowed<const byte> data() const noexcept {
            return storage_.data();
        }
        [[nodiscard]] view<byte> bytes() noexcept {
            return storage_;
        }
        [[nodiscard]] view<const byte> bytes() const noexcept {
            return storage_;
        }

        void resize(size_t size) {
            storage_.resize(size);
        }
        void clear() noexcept {
            storage_.clear();
        }
        void swap(buffer& other) noexcept {
            storage_.swap(other.storage_);
        }
        [[nodiscard]] resource_allocator<byte> get_allocator() const noexcept {
            return storage_.get_allocator();
        }

    private:
        vector<byte, resource_allocator<byte>> storage_;
    };

    class RAFTPP_API aligned_buffer {
    public:
        aligned_buffer() noexcept = default;
        // Alignment must be a power of two and at least alignof(max_align_t).
        aligned_buffer(size_t size, size_t alignment, resource_allocator<byte> allocator = {});

        aligned_buffer(const aligned_buffer&) = delete;
        aligned_buffer& operator=(const aligned_buffer&) = delete;
        aligned_buffer(aligned_buffer&&) noexcept = default;
        aligned_buffer& operator=(aligned_buffer&&) noexcept = default;

        [[nodiscard]] size_t size() const noexcept {
            return storage_ ? storage_.get_deleter().size : 0;
        }
        [[nodiscard]] bool empty() const noexcept {
            return size() == 0;
        }
        [[nodiscard]] size_t alignment() const noexcept {
            return storage_.get_deleter().alignment;
        }
        [[nodiscard]] borrowed<byte> data() noexcept {
            return storage_.get();
        }
        [[nodiscard]] borrowed<const byte> data() const noexcept {
            return storage_.get();
        }
        [[nodiscard]] view<byte> bytes() noexcept {
            return {data(), size()};
        }
        [[nodiscard]] view<const byte> bytes() const noexcept {
            return {data(), size()};
        }
        void swap(aligned_buffer& other) noexcept;
        [[nodiscard]] resource_allocator<byte> get_allocator() const noexcept {
            return storage_.get_deleter().allocator;
        }

    private:
        struct deleter {
            resource_allocator<byte> allocator;
            size_t size = 0;
            size_t alignment = alignof(max_align_t);
            void operator()(borrowed<byte> pointer) const noexcept {
                allocator.resource()->deallocate(pointer, size, alignment);
            }
        };

        owned<byte, deleter> storage_{nullptr, deleter{}};
    };

    // An immutable range whose standard shared_ptr retains the complete allocation.
    // Aliasing shared_ptrs let disjoint entries keep one ordinary or aligned batch alive.
    class RAFTPP_API shared_buffer {
    public:
        shared_buffer() noexcept = default;
        explicit shared_buffer(buffer storage);
        explicit shared_buffer(aligned_buffer storage);
        shared_buffer(const shared_buffer&) noexcept = default;
        shared_buffer& operator=(const shared_buffer&) noexcept = default;
        shared_buffer(shared_buffer&& other) noexcept
            : storage_(std::move(other.storage_)), size_(std::exchange(other.size_, 0)) {}
        shared_buffer& operator=(shared_buffer&& other) noexcept {
            if (this != &other) {
                storage_ = std::move(other.storage_);
                size_ = std::exchange(other.size_, 0);
            }
            return *this;
        }

        [[nodiscard]] static shared_buffer copy(view<const byte> input,
                                                resource_allocator<byte> allocator = {});
        [[nodiscard]] shared_buffer slice(size_t offset, size_t count = std::dynamic_extent) const;
        [[nodiscard]] size_t size() const noexcept {
            return size_;
        }
        [[nodiscard]] bool empty() const noexcept {
            return size_ == 0;
        }
        [[nodiscard]] view<const byte> bytes() const noexcept {
            return {storage_.get(), size_};
        }
        [[nodiscard]] const shared<const byte>& storage() const noexcept {
            return storage_;
        }
        [[nodiscard]] bool shares_storage_with(const shared_buffer& other) const noexcept;

    private:
        shared<const byte> storage_;
        size_t size_ = 0;
    };

}

#endif // RAFTPP_BUFFER_HPP
