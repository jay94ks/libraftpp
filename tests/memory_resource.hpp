#ifndef RAFTPP_TEST_MEMORY_RESOURCE_HPP
#define RAFTPP_TEST_MEMORY_RESOURCE_HPP
#include <raftpp/memory.hpp>
#include <atomic>
#include <mutex>
namespace memory_testing {
    namespace r = raftpp;
    struct statistics {
        std::atomic<r::size_t> allocated{0}, freed{0}, live{0};
        std::atomic<bool> destroyed{false}, mismatch{false};
        std::atomic<int> fail_after{-1};
    };
    class resource final : public r::pmr::memory_resource {
    public:
        r::shared<statistics> stats;
        explicit resource(r::shared<statistics> value) : stats(std::move(value)) {}
        ~resource() override {
            if (!allocations.empty())
                stats->mismatch = true;
            stats->destroyed = true;
        }

    private:
        std::mutex mutex;
        r::map<void*, r::pair<r::size_t, r::size_t>> allocations;
        void* do_allocate(r::size_t bytes, r::size_t alignment) override {
            int position = stats->fail_after.load();
            if (position >= 0 && stats->fail_after.fetch_sub(1) == 0)
                throw r::bad_alloc{};
            auto pointer = r::pmr::new_delete_resource()->allocate(bytes, alignment);
            try {
                std::lock_guard lock{mutex};
                allocations.emplace(pointer, r::pair{bytes, alignment});
            } catch (...) {
                r::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
                throw;
            }
            ++stats->allocated;
            ++stats->live;
            return pointer;
        }
        void do_deallocate(void* pointer, r::size_t bytes, r::size_t alignment) override {
            {
                std::lock_guard lock{mutex};
                auto position = allocations.find(pointer);
                if (position == allocations.end() || position->second != r::pair{bytes, alignment})
                    stats->mismatch = true;
                if (position != allocations.end())
                    allocations.erase(position);
            }
            ++stats->freed;
            --stats->live;
            r::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const r::pmr::memory_resource& other) const noexcept override {
            return this == &other;
        }
    };
}
#endif
