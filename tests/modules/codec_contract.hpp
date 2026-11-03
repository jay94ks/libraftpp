#ifndef RAFTPP_TEST_CODEC_CONTRACT_HPP
#define RAFTPP_TEST_CODEC_CONTRACT_HPP
#include <raftpp/compression.hpp>
#include <algorithm>
#include <iostream>

inline bool check_codec(const raftpp::compression_codec& codec) {
    using namespace raftpp;
    auto equal = [](const buffer& a, view<const byte> b) {
        return a.size() == b.size() && std::equal(a.bytes().begin(), a.bytes().end(), b.begin());
    };
    const array<byte, 4> sentinel{byte{1}, byte{2}, byte{3}, byte{4}};
    for (size_t size : {size_t{0}, size_t{1}, size_t{65536}, size_t{150123}}) {
        buffer input{size};
        for (size_t i = 0; i < size; ++i)
            input.data()[i] = static_cast<byte>((i * 31 + i / 11) % 251);
        buffer encoded;
        if (codec.compress(input.bytes(), encoded) || encoded.empty())
            return false;
        buffer decoded;
        if (codec.decompress(encoded.bytes(), decoded, size) || !equal(decoded, input.bytes()))
            return false;
        if (size) {
            decoded = buffer::copy(sentinel);
            if (codec.decompress(encoded.bytes(), decoded, size - 1) != errc::too_big ||
                !equal(decoded, sentinel))
                return false;
        }
        for (size_t length : {size_t{0}, size_t{1}, encoded.size() / 2, encoded.size() - 1}) {
            decoded = buffer::copy(sentinel);
            if (!codec.decompress(encoded.bytes().first(length), decoded, size) ||
                !equal(decoded, sentinel))
                return false;
        }
        auto extra = buffer::copy(encoded.bytes());
        extra.resize(extra.size() + 1);
        decoded = buffer::copy(sentinel);
        if (!codec.decompress(extra.bytes(), decoded, size) || !equal(decoded, sentinel))
            return false;
        extra.data()[0] = byte{0xff};
        if (!codec.decompress(extra.bytes(), decoded, size) || !equal(decoded, sentinel))
            return false;
        auto alias = buffer::copy(input.bytes());
        if (codec.compress(alias.bytes(), alias) || codec.decompress(alias.bytes(), alias, size) ||
            !equal(alias, input.bytes()))
            return false;
    }
    struct fail_resource final : pmr::memory_resource {
        bool fail = false;
        void* do_allocate(size_t bytes, size_t alignment) override {
            if (fail)
                throw bad_alloc{};
            return pmr::new_delete_resource()->allocate(bytes, alignment);
        }
        void do_deallocate(void* pointer, size_t bytes, size_t alignment) override {
            pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const pmr::memory_resource& other) const noexcept override {
            return this == &other;
        }
    };
    auto resource = std::make_shared<fail_resource>();
    buffer output{resource_allocator<byte>{resource}};
    buffer encoded;
    if (codec.compress(sentinel, encoded))
        return false;
    resource->fail = true;
    if (codec.compress(sentinel, output) != errc::out_of_memory || !output.empty() ||
        output.get_allocator().resource() != resource.get())
        return false;
    if (codec.decompress(encoded.bytes(), output, sentinel.size()) != errc::out_of_memory ||
        !output.empty() || output.get_allocator().resource() != resource.get())
        return false;
    resource->fail = false;
    if (codec.decompress(encoded.bytes(), output, sentinel.size()) || !equal(output, sentinel) ||
        output.get_allocator().resource() != resource.get())
        return false;
    return true;
}
#endif
