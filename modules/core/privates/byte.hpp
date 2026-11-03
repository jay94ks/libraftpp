#ifndef RAFTPP_DETAIL_BYTE_HPP
#define RAFTPP_DETAIL_BYTE_HPP

#include <raftpp/common.hpp>
#include <algorithm>

namespace raftpp::detail {

    template <typename T>
        requires std::is_unsigned_v<T>
    T load_little_endian(view<const byte, sizeof(T)> input) noexcept {
        T value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<T>(std::to_integer<T>(input[i]) << (8 * i));
        return value;
    }

    template <typename T>
        requires std::is_unsigned_v<T>
    void store_little_endian(T value, view<byte, sizeof(T)> output) noexcept {
        for (size_t i = 0; i < sizeof(T); ++i)
            output[i] = static_cast<byte>(value >> (8 * i));
    }

    class byte_reader {
    public:
        explicit byte_reader(view<const byte> input) noexcept : remaining_(input) {}
        template <typename T>
            requires std::is_unsigned_v<T>
        bool read(T& value) noexcept {
            if (remaining_.size() < sizeof(T))
                return false;
            value = load_little_endian<T>(remaining_.template first<sizeof(T)>());
            remaining_ = remaining_.subspan(sizeof(T));
            return true;
        }
        bool skip(size_t size) noexcept {
            if (size > remaining_.size())
                return false;
            remaining_ = remaining_.subspan(size);
            return true;
        }
        bool take(size_t size, view<const byte>& value) noexcept {
            if (size > remaining_.size())
                return false;
            value = remaining_.first(size);
            remaining_ = remaining_.subspan(size);
            return true;
        }
        [[nodiscard]] size_t remaining() const noexcept {
            return remaining_.size();
        }

    private:
        view<const byte> remaining_;
    };

    // A writer is used only after validating and allocating the complete output.
    class byte_writer {
    public:
        explicit byte_writer(view<byte> output) noexcept : remaining_(output) {}
        template <typename T>
            requires std::is_unsigned_v<T>
        void write(T value) noexcept {
            store_little_endian<T>(value, remaining_.template first<sizeof(T)>());
            remaining_ = remaining_.subspan(sizeof(T));
        }
        void write(view<const byte> value) noexcept {
            if (!value.empty())
                std::copy(value.begin(), value.end(), remaining_.begin());
            remaining_ = remaining_.subspan(value.size());
        }

    private:
        view<byte> remaining_;
    };

}

#endif // RAFTPP_DETAIL_BYTE_HPP
