#include <raftpp/buffer.hpp>

#include <algorithm>
#include <bit>

namespace raftpp {

    aligned_buffer::aligned_buffer(size_t size, size_t alignment,
                                   resource_allocator<byte> allocator)
        : storage_(nullptr, deleter{std::move(allocator), size, alignment}) {
        if (alignment < alignof(max_align_t) || !std::has_single_bit(alignment)) {
            throw invalid_argument("invalid buffer alignment");
        }
        if (size != 0) {
            storage_.reset(static_cast<byte*>(
                storage_.get_deleter().allocator.resource()->allocate(size, alignment)));
            std::fill_n(storage_.get(), size, byte{0});
        }
    }

    void aligned_buffer::swap(aligned_buffer& other) noexcept {
        storage_.swap(other.storage_);
    }

    shared_buffer::shared_buffer(buffer storage) {
        auto owner = std::allocate_shared<buffer>(
            resource_allocator<buffer>{storage.get_allocator()}, std::move(storage));
        size_ = owner->size();
        storage_ = shared<const byte>{owner, owner->data()};
    }

    shared_buffer::shared_buffer(aligned_buffer storage) {
        auto owner = std::allocate_shared<aligned_buffer>(
            resource_allocator<aligned_buffer>{storage.get_allocator()}, std::move(storage));
        size_ = owner->size();
        storage_ = shared<const byte>{owner, owner->data()};
    }

    shared_buffer shared_buffer::copy(view<const byte> input, resource_allocator<byte> allocator) {
        return shared_buffer{buffer::copy(input, std::move(allocator))};
    }

    shared_buffer shared_buffer::slice(size_t offset, size_t count) const {
        if (offset > size_) {
            throw out_of_range("buffer slice offset");
        }
        if (count == std::dynamic_extent) {
            count = size_ - offset;
        }
        if (count > size_ - offset) {
            throw out_of_range("buffer slice length");
        }
        auto pointer = storage_.get();
        if (offset != 0) {
            pointer += offset;
        }
        shared_buffer result;
        result.storage_ = shared<const byte>{storage_, pointer};
        result.size_ = count;
        return result;
    }

    bool shared_buffer::shares_storage_with(const shared_buffer& other) const noexcept {
        return storage_.use_count() != 0 && !storage_.owner_before(other.storage_) &&
               !other.storage_.owner_before(storage_);
    }

}
