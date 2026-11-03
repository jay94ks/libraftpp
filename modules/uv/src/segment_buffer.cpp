// C++ port of cowsql/raft uvSegmentBuffer routines; see LICENSE.md.
#include "segment_buffer.hpp"
#include "byte.hpp"
#include <algorithm>
#include <bit>

namespace raftpp::detail {
    namespace {
        template <typename F> error_code guarded(F&& action) noexcept {
            try {
                return action();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            } catch (...) {
                return errc::invalid;
            }
        }
    }
    segment_buffer::segment_buffer(size_t block_size) : block_size_(block_size) {
        if (block_size < alignof(max_align_t) || block_size < 8 || !std::has_single_bit(block_size))
            throw invalid_argument{"invalid block size"};
    }
    error_code segment_buffer::reserve(size_t size) noexcept {
        return guarded([&]() -> error_code {
            if (size > numeric_limits<size_t>::max() - (block_size_ - 1))
                return errc::too_big;
            const auto rounded = (size + block_size_ - 1) / block_size_ * block_size_;
            if (rounded <= storage_.size())
                return {};
            aligned_buffer next{rounded, block_size_};
            std::ranges::fill(next.bytes(), byte{});
            if (!storage_.empty())
                std::ranges::copy(storage_.bytes(), next.bytes().begin());
            storage_ = std::move(next);
            return {};
        });
    }
    error_code segment_buffer::format() noexcept {
        if (size_ != 0)
            return errc::invalid;
        if (auto error = reserve(8))
            return error;
        store_little_endian<uint64_t>(disk_format_version, storage_.bytes().first<8>());
        size_ = 8;
        return {};
    }
    error_code segment_buffer::append(view<const byte> record) noexcept {
        if (record.empty() || record.size() % 8 != 0)
            return errc::invalid;
        if (record.size() > numeric_limits<size_t>::max() - size_)
            return errc::too_big;
        const auto base = reinterpret_cast<uintptr_t>(storage_.data());
        const auto source = reinterpret_cast<uintptr_t>(record.data());
        const bool aliases = !storage_.empty() && source >= base && source - base < storage_.size();
        const auto offset = aliases ? static_cast<size_t>(source - base) : 0;
        if (aliases && (offset > size_ || record.size() > size_ - offset))
            return errc::invalid;
        if (auto error = reserve(size_ + record.size()))
            return error;
        if (aliases)
            record = storage_.bytes().subspan(offset, record.size());
        std::ranges::copy(record, storage_.bytes().begin() + static_cast<ptrdiff_t>(size_));
        size_ += record.size();
        return {};
    }
    error_code segment_buffer::freeze(shared_buffer& output) const noexcept {
        return guarded([&]() -> error_code {
            if (size_ == 0)
                return errc::invalid;
            const auto rounded = (size_ + block_size_ - 1) / block_size_ * block_size_;
            aligned_buffer result{rounded, block_size_};
            std::ranges::copy(bytes(), result.bytes().begin());
            std::ranges::fill(result.bytes().subspan(size_), byte{});
            output = shared_buffer{std::move(result)};
            return {};
        });
    }
    error_code segment_buffer::reset(size_t retained_block) noexcept {
        if (storage_.empty() || size_ == 0 || retained_block > size_ / block_size_)
            return errc::invalid;
        if (retained_block == 0) {
            size_ = 0;
            std::ranges::fill(storage_.bytes().first(block_size_), byte{});
            return {};
        }
        if (size_ % block_size_ == 0 || retained_block != size_ / block_size_)
            return errc::invalid;
        std::ranges::copy(storage_.bytes().subspan(retained_block * block_size_, block_size_),
                          storage_.bytes().begin());
        size_ %= block_size_;
        return {};
    }
}
