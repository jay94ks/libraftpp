#ifndef RAFTPP_MEMORY_HPP
#define RAFTPP_MEMORY_HPP

#include <raftpp/common.hpp>
#include <raftpp/export.hpp>

namespace raftpp {
    template <typename T> struct resource_deleter {
        resource_allocator<T> allocator;
        void operator()(borrowed<T> pointer) const noexcept {
            if (pointer)
                pmr::polymorphic_allocator<T>{allocator.resource()}.delete_object(pointer);
        }
    };
    template <typename T> using resource_owned = owned<T, resource_deleter<T>>;

    // C++20 has allocate_shared and PMR new_object/delete_object, but no owning
    // allocate_unique. This adapter uses those standard construction primitives.
    template <typename T, typename... Args>
    [[nodiscard]] resource_owned<T> allocate_owned(resource_allocator<T> allocator,
                                                   Args&&... args) {
        static_assert(!std::is_array_v<T>);
        auto pointer = pmr::polymorphic_allocator<T>{allocator.resource()}.template new_object<T>(
            std::forward<Args>(args)...);
        return resource_owned<T>{pointer, resource_deleter<T>{std::move(allocator)}};
    }
}
#endif // RAFTPP_MEMORY_HPP
