#ifndef RAFTPP_COMMON_HPP
#define RAFTPP_COMMON_HPP

#include <raftpp/export.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <filesystem>
#include <list>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <source_location>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace raftpp {

    using std::byte;
    using std::ptrdiff_t;
    using std::size_t;
    using std::max_align_t;
    using std::align_val_t;

    using uint8_t = std::uint8_t;
    using uint16_t = std::uint16_t;
    using uint32_t = std::uint32_t;
    using uint64_t = std::uint64_t;
    using uintptr_t = std::uintptr_t;

    using int8_t = std::int8_t;
    using int16_t = std::int16_t;
    using int32_t = std::int32_t;
    using int64_t = std::int64_t;

    // Exclusive ownership. Moving transfers ownership; Deleter handles native resources.
    template <typename T, typename Deleter = std::default_delete<T>>
    using owned = std::unique_ptr<T, Deleter>;

    template <typename T, typename Deleter = std::default_delete<T>>
    using unique = owned<T, Deleter>;

    // Shared ownership keeps the object alive, but does not synchronize access to it.
    template <typename T> using shared = std::shared_ptr<T>;

    // Non-owning observation of shared state; lock() acquires ownership if still alive.
    template <typename T> using weak = std::weak_ptr<T>;

    // Nullable non-owning pointer. The caller must keep the object alive.
    template <typename T> using borrowed = T*;

    // Required non-owning reference. Construct from an lvalue; access via get().
    // Like borrowed<T>, it does not extend the object's lifetime.
    template <typename T> using ref = std::reference_wrapper<T>;

    // Non-owning contiguous range. Use view<const T> for read-only access.
    // Neither the view nor its copies keep the backing storage alive.
    template <typename T, size_t Extent = std::dynamic_extent> using view = std::span<T, Extent>;

    // Reuse standard types and their complete template interfaces unchanged.
    using std::array;
    using std::deque;
    using std::list;
    using std::map;
    using std::pair;
    using std::optional;
    using std::string;
    using std::string_view;
    using std::variant;
    using std::vector;
    using std::function;
    using std::source_location;
    using std::atomic_bool;
    using std::atomic_ref;
    using std::remove_cvref_t;
    using std::decay_t;
    using std::enable_shared_from_this;

    using std::error_category;
    using std::error_code;
    using std::error_condition;
    using std::system_error;
    using std::bad_alloc;
    using std::length_error;
    using std::numeric_limits;
    using std::invalid_argument;
    using std::out_of_range;

    namespace chrono = std::chrono;
    namespace pmr = std::pmr;
    namespace fs = std::filesystem;

    // A null selection restores new_delete_resource. Existing allocations keep
    // the resource that created them; switching never changes their deleter.
    [[nodiscard]] RAFTPP_API shared<pmr::memory_resource> get_memory_resource() noexcept;
    RAFTPP_API shared<pmr::memory_resource>
    set_memory_resource(shared<pmr::memory_resource> resource) noexcept;
    RAFTPP_API void reset_memory_resource() noexcept;

    // Standard PMR allocation with shared ownership of its resource. The
    // ownership is needed even after an allocate_shared object's destruction,
    // while its weak references still retain the standard control block.
    template <typename T> class resource_allocator {
    public:
        using value_type = T;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using propagate_on_container_copy_assignment = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;
        using is_always_equal = std::false_type;

        resource_allocator() noexcept : owner_(get_memory_resource()) {}
        explicit resource_allocator(shared<pmr::memory_resource> resource) noexcept
            : owner_(resource ? std::move(resource) : get_memory_resource()) {}
        resource_allocator(const resource_allocator&) noexcept = default;
        resource_allocator& operator=(const resource_allocator&) noexcept = default;
        // Standard containers must remain usable after moving their allocator.
        resource_allocator(resource_allocator&& other) noexcept : owner_(other.owner_) {}
        resource_allocator& operator=(resource_allocator&& other) noexcept {
            owner_ = other.owner_;
            return *this;
        }
        template <typename U>
        resource_allocator(const resource_allocator<U>& other) noexcept : owner_(other.owner()) {}

        [[nodiscard]] T* allocate(size_t count) {
            return pmr::polymorphic_allocator<T>{resource()}.allocate(count);
        }
        void deallocate(T* pointer, size_t count) noexcept {
            pmr::polymorphic_allocator<T>{resource()}.deallocate(pointer, count);
        }
        [[nodiscard]] borrowed<pmr::memory_resource> resource() const noexcept {
            return owner_.get();
        }
        [[nodiscard]] const shared<pmr::memory_resource>& owner() const noexcept {
            return owner_;
        }
        template <typename U> bool operator==(const resource_allocator<U>& other) const noexcept {
            return resource() == other.resource();
        }

    private:
        shared<pmr::memory_resource> owner_;
    };

    // Allocator-aware standard container aliases keep their full interfaces.
    template <typename T, typename Allocator = resource_allocator<T>>
    using resource_vector = vector<T, Allocator>;
    template <typename T, typename Allocator = resource_allocator<T>>
    using resource_deque = deque<T, Allocator>;
    template <typename T, typename Allocator = resource_allocator<T>>
    using resource_list = list<T, Allocator>;
    template <typename Key, typename Value, typename Compare = std::less<Key>,
              typename Allocator = resource_allocator<pair<const Key, Value>>>
    using resource_map = map<Key, Value, Compare, Allocator>;
    template <typename Char, typename Traits = std::char_traits<Char>,
              typename Allocator = resource_allocator<Char>>
    using resource_basic_string = std::basic_string<Char, Traits, Allocator>;
    using resource_string = resource_basic_string<char>;

}

#endif // RAFTPP_COMMON_HPP
