#include <cstddef>
#include <cstdlib>
#include <new>
#include <limits>

namespace allocation_failure {
    thread_local std::ptrdiff_t remaining = -1;
    void after(std::ptrdiff_t count) noexcept {
        remaining = count;
    }
    void point() {
        if (remaining == 0) {
            remaining = -1;
            throw std::bad_alloc{};
        }
        if (remaining > 0)
            --remaining;
    }
}

// A test-only, one-shot failure of a standard C++ allocation. Keeping these
// replacements in a separate translation unit also tests the compiled library.
void* operator new(std::size_t size) {
    allocation_failure::point();
    if (auto pointer = std::malloc(size == 0 ? 1 : size))
        return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* pointer) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

// PMR new_delete_resource uses aligned new even for ordinary objects on this
// standard library. Cover both forms so the same one-shot injection remains
// meaningful for allocator-aware buffers and shared control blocks.
void* operator new(std::size_t size, std::align_val_t alignment) {
    allocation_failure::point();
    const auto requested = static_cast<std::size_t>(alignment);
    const auto effective =
        requested < alignof(std::max_align_t) ? alignof(std::max_align_t) : requested;
    if (size == 0)
        size = 1;
    if (size > std::numeric_limits<std::size_t>::max() - (effective - 1))
        throw std::bad_alloc{};
    const auto rounded = (size + effective - 1) / effective * effective;
    if (auto pointer = std::aligned_alloc(effective, rounded))
        return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    std::free(pointer);
}
