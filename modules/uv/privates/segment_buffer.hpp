#ifndef RAFTPP_DETAIL_SEGMENT_BUFFER_HPP
#define RAFTPP_DETAIL_SEGMENT_BUFFER_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/encoding.hpp>
#include <raftpp/config.hpp>

#if RAFTPP_WITH_UV
namespace raftpp::detail {
    class RAFTPP_UV_API segment_buffer {
    public:
        explicit segment_buffer(size_t block_size);
        [[nodiscard]] error_code format() noexcept;
        [[nodiscard]] error_code append(view<const byte> encoded_record) noexcept;
        // Immutable aligned copy: retries and completion retain the same bytes.
        [[nodiscard]] error_code freeze(shared_buffer& output) const noexcept;
        [[nodiscard]] error_code reset(size_t retained_block) noexcept;
        [[nodiscard]] size_t size() const noexcept {
            return size_;
        }
        [[nodiscard]] size_t block_size() const noexcept {
            return block_size_;
        }
        [[nodiscard]] view<const byte> bytes() const noexcept {
            return storage_.bytes().first(size_);
        }

    private:
        [[nodiscard]] error_code reserve(size_t size) noexcept;
        aligned_buffer storage_;
        size_t block_size_, size_ = 0;
    };
}
#endif
#endif // RAFTPP_DETAIL_SEGMENT_BUFFER_HPP
