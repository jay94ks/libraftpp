// C++ ownership/PMR replacement for cowsql/raft heap.c; see LICENSE.md.
#include <raftpp/memory.hpp>

namespace raftpp {
    namespace {
        std::atomic<shared<pmr::memory_resource>> selected;
        shared<pmr::memory_resource> builtin() noexcept {
            // The standard resource has static lifetime; aliasing an empty
            // shared_ptr avoids allocating a control block for the default.
            return {shared<pmr::memory_resource>{}, pmr::new_delete_resource()};
        }
    }
    shared<pmr::memory_resource> get_memory_resource() noexcept {
        auto resource = selected.load(std::memory_order_acquire);
        return resource ? resource : builtin();
    }
    shared<pmr::memory_resource>
    set_memory_resource(shared<pmr::memory_resource> resource) noexcept {
        auto previous = selected.exchange(std::move(resource), std::memory_order_acq_rel);
        return previous ? previous : builtin();
    }
    void reset_memory_resource() noexcept {
        (void)set_memory_resource({});
    }
}
