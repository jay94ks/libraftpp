#include <raftpp.hpp>
#include <raftpp/detail/core.hpp>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <source_location>
#include "memory_resource.hpp"

namespace r = raftpp;
namespace {
    void check(bool condition, r::string_view text,
               std::source_location where = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{text});
    }
    using memory_testing::statistics;
    using memory_testing::resource;
    struct selection {
        r::shared<r::pmr::memory_resource> previous = r::get_memory_resource();
        ~selection() {
            (void)r::set_memory_resource(std::move(previous));
        }
    };
    void defaults() {
        selection restore;
        r::reset_memory_resource();
        check(r::get_memory_resource().get() == r::pmr::new_delete_resource(),
              "default is the standard new/delete resource");
        auto stats = std::make_shared<statistics>();
        auto selected = std::make_shared<resource>(stats);
        auto previous = r::set_memory_resource(selected);
        check(previous.get() == r::pmr::new_delete_resource() &&
                  r::get_memory_resource() == selected,
              "set/get return correct resources");
        {
            r::buffer empty;
            check(empty.empty() && stats->allocated == 0, "empty buffer does not allocate");
            r::buffer bytes{32};
            check(stats->allocated == 1 && stats->live == 1,
                  "buffer payload uses selected resource");
            check(std::ranges::all_of(bytes.bytes(), [](auto value) { return value == r::byte{}; }),
                  "buffer storage is zero initialized");
            std::ranges::fill(bytes.bytes(), r::byte{42});
            r::reset_memory_resource();
            bytes.resize(64);
            check(bytes.get_allocator().resource() == selected.get(),
                  "growth retains original resource after reset");
            check(std::ranges::all_of(bytes.bytes().first(32),
                                      [](auto value) { return value == r::byte{42}; }),
                  "resize preserves its existing prefix");
            check(std::ranges::all_of(bytes.bytes().subspan(32),
                                      [](auto value) { return value == r::byte{}; }),
                  "new buffer bytes are initialized");
        }
        check(stats->allocated == stats->freed && stats->live == 0 && !stats->mismatch,
              "exact resource/size/alignment deallocation");
        auto old = r::set_memory_resource({});
        check(old.get() == r::pmr::new_delete_resource(), "null selection resets default");
    }
    void alignment() {
        selection restore;
        auto sa = std::make_shared<statistics>(), sb = std::make_shared<statistics>();
        auto a = std::make_shared<resource>(sa), b = std::make_shared<resource>(sb);
        (void)r::set_memory_resource(a);
        {
            r::aligned_buffer first{123, 4096};
            check(reinterpret_cast<r::uintptr_t>(first.data()) % 4096 == 0 &&
                      std::ranges::all_of(first.bytes(), [](auto v) { return v == r::byte{}; }),
                  "arbitrary-sized aligned storage");
            (void)r::set_memory_resource(b);
            r::aligned_buffer second{512, 512};
            first.swap(second);
            check(first.get_allocator().resource() == b.get() &&
                      second.get_allocator().resource() == a.get(),
                  "aligned swap moves matching resources");
            first = std::move(second);
            check(second.empty() && first.alignment() == 4096 && first.size() == 123,
                  "aligned move transfers resource and metadata");
            bool rejected = false;
            try {
                r::aligned_buffer invalid{1, 3};
            } catch (const r::invalid_argument&) {
                rejected = true;
            }
            check(rejected, "invalid alignment fails before allocation");
            r::buffer x{8, r::resource_allocator<r::byte>{a}},
                y{12, r::resource_allocator<r::byte>{b}};
            x.swap(y);
            check(x.get_allocator().resource() == b.get() &&
                      y.get_allocator().resource() == a.get(),
                  "ordinary swap propagates unequal allocators");
            y = std::move(x);
            x.resize(3);
            check(y.size() == 12 && y.get_allocator().resource() == b.get() &&
                      x.get_allocator().resource() == b.get(),
                  "moved-from allocator stays usable");
        }
        r::reset_memory_resource();
        check(!sa->mismatch && !sb->mismatch && sa->live == 0 && sb->live == 0,
              "aligned and vector resources balance after swaps/moves");
    }
    void lifetime() {
        selection restore;
        r::reset_memory_resource();
        for (bool aligned : {false, true}) {
            auto stats = std::make_shared<statistics>();
            r::weak<r::pmr::memory_resource> owner;
            r::weak<const r::byte> weak;
            r::shared_buffer retained;
            {
                auto memory = std::make_shared<resource>(stats);
                owner = memory;
                (void)r::set_memory_resource(memory);
                r::shared_buffer value = aligned ? r::shared_buffer{r::aligned_buffer{123, 4096}}
                                                 : r::shared_buffer{r::buffer{123}};
                retained = value.slice(100, 23);
                weak = value.storage();
                r::reset_memory_resource();
                memory.reset();
            }
            check(!owner.expired() && !stats->destroyed && retained.size() == 23,
                  "slice owns payload and originating resource");
            retained = {};
            check(!owner.expired() && !stats->destroyed && weak.expired() && stats->live == 1,
                  "expired weak payload still retains control-block resource");
            weak.reset();
            check(owner.expired() && stats->destroyed && stats->live == 0 && !stats->mismatch,
                  "last weak reference releases resource after control block");
        }
    }
    void objects() {
        selection restore;
        auto stats = std::make_shared<statistics>();
        auto memory = std::make_shared<resource>(stats);
        r::weak<r::pmr::memory_resource> observer = memory;
        struct alignas(4096) object {
            unsigned* destroyed;
            explicit object(unsigned* value) : destroyed(value) {}
            ~object() {
                ++*destroyed;
            }
        };
        unsigned destroyed = 0;
        r::weak<object> weak;
        {
            r::resource_allocator<object> allocator{memory};
            auto owned = r::allocate_owned<object>(allocator, &destroyed);
            check(reinterpret_cast<r::uintptr_t>(owned.get()) % alignof(object) == 0,
                  "owned object alignment");
            auto shared = std::allocate_shared<object>(allocator, &destroyed);
            weak = shared;
            auto rebound = r::resource_allocator<int>{allocator};
            r::vector<int, r::resource_allocator<int>> values{rebound};
            values.assign(123, 7);
            r::list<int, r::resource_allocator<int>> nodes{rebound};
            nodes.push_back(42);
            memory.reset();
            check(!observer.expired() && values[122] == 7 && nodes.front() == 42,
                  "allocator rebinding retains resource for standard containers");
        }
        check(destroyed == 2 && !observer.expired() && weak.expired() && stats->live == 1,
              "object destructors precede weak control-block destruction");
        weak.reset();
        check(observer.expired() && stats->live == 0 && !stats->mismatch,
              "shared control block frees through its original resource");
        struct throws {
            throws() {
                throw r::invalid_argument("constructor");
            }
        };
        auto other = std::make_shared<resource>(stats);
        bool failed = false;
        {
            auto immutable = std::allocate_shared<const int>(r::resource_allocator<int>{other}, 42);
            check(*immutable == 42, "const objects use standard cv-unqualified allocators");
        }
        try {
            (void)r::allocate_owned<throws>(r::resource_allocator<throws>{other});
        } catch (const r::invalid_argument&) {
            failed = true;
        }
        check(failed && stats->live == 0,
              "standard PMR constructor failure releases allocated storage");
        failed = false;
        try {
            (void)r::resource_allocator<r::uint64_t>{other}.allocate(
                r::numeric_limits<r::size_t>::max());
        } catch (const std::bad_array_new_length&) {
            failed = true;
        }
        check(failed && stats->live == 0,
              "standard allocator detects count overflow before allocation");
    }
    void containers() {
        selection restore;
        auto sa = std::make_shared<statistics>(), sb = std::make_shared<statistics>();
        auto a = std::make_shared<resource>(sa), b = std::make_shared<resource>(sb);
        (void)r::set_memory_resource(a);
        {
            r::detail::trail trail;
            r::detail::log log;
            r::detail::core_update update;
            r::detail::candidate_state candidate;
            r::detail::follower_state follower;
            r::resource_vector<r::detail::progress> peers;
            r::resource_vector<r::shared<const r::entry>> acquired;
            r::configuration current;
            check(!current.add(1, "one", r::role::voter) && !current.add(2, "two", r::role::voter),
                  "prepare configuration");
            auto next = current;
            check(!next.add(3, "three", r::role::voter), "prepare membership change");
            (void)r::set_memory_resource(b);
            check(!trail.start(0, 0, 1) && !log.start(0, 0, 1), "start allocator-aware caches");
            candidate.votes.resize(32);
            follower.leader_address.assign(512, 'x');
            check(candidate.votes.get_allocator().resource() == a.get() &&
                      follower.leader_address.get_allocator().resource() == a.get(),
                  "role containers retain creation resource after selection changes");
            for (r::term term = 1; term <= 128; ++term) {
                check(!trail.append(term) && !log.append({term, r::entry_type::command, {}}),
                      "grow trail, cache, and reference index through original resource");
            }
            check(!r::detail::enqueue_message(update, 2, "two", r::request_vote{}),
                  "enqueue allocator-aware message");
            check(update.messages.get_allocator().resource() == a.get(),
                  "update message storage captures its own resource");
            check(!r::detail::build_progress(current, 1, 128, 127, peers),
                  "build resource progress");
            peers[1].match_index = 42;
            check(!r::detail::rebuild_progress(current, next, 128, peers),
                  "rebuild resource progress");
            check(peers.size() == 3 && peers[1].match_index == 42 && peers[2].next_index == 129 &&
                      peers.get_allocator().resource() == a.get(),
                  "membership preserves peer state and output allocator");
            sa->fail_after = 0;
            check(r::detail::build_progress(current, 1, 128, 127, peers) ==
                          r::errc::out_of_memory &&
                      peers.size() == 3 && peers[1].match_index == 42,
                  "failed progress build preserves output");
            sa->fail_after = 0;
            check(r::detail::rebuild_progress(next, current, 128, peers) ==
                          r::errc::out_of_memory &&
                      peers.size() == 3 && peers[1].match_index == 42,
                  "failed membership rebuild preserves output");
            check(!log.acquire(1, acquired, 1), "acquire with caller resource");
            auto first = acquired.front();
            sa->fail_after = 0;
            check(log.acquire(1, acquired) == r::errc::out_of_memory && acquired.size() == 1 &&
                      acquired.front() == first,
                  "failed acquire preserves handles and caller resource");
            sa->fail_after = 0;
            check(log.append({129, r::entry_type::command, {}}) == r::errc::out_of_memory &&
                      log.size() == 128 && log.get(129) == nullptr,
                  "reference-index allocation failure leaves cache unchanged");
            check(!log.append({129, r::entry_type::command, {}}),
                  "retry after reference-index failure");
            sa->fail_after = 0;
            bool failed = false;
            for (r::term term = 129; term <= 2048; ++term) {
                auto error = trail.append(term);
                if (error) {
                    check(error == r::errc::out_of_memory && trail.last_index() == term - 1 &&
                              trail.last_term() == term - 1,
                          "failed trail growth preserves term and index");
                    failed = true;
                    break;
                }
            }
            check(failed, "trail growth reaches selected-resource failure");
            sa->fail_after = -1;
        }
        r::reset_memory_resource();
        check(sa->allocated > 10 && sb->allocated > 128,
              "old container and new object resources were both exercised");
        check(sa->live == 0 && sb->live == 0 && !sa->mismatch && !sb->mismatch,
              "container cleanup uses original byte counts and alignment");
    }
    void failures() {
        selection restore;
        auto stats = std::make_shared<statistics>();
        auto memory = std::make_shared<resource>(stats);
        (void)r::set_memory_resource(memory);
        {
            r::buffer value{16};
            std::ranges::fill(value.bytes(), r::byte{42});
            auto pointer = value.data();
            stats->fail_after = 0;
            bool failed = false;
            try {
                value.resize(4096);
            } catch (const r::bad_alloc&) {
                failed = true;
            }
            check(failed && value.size() == 16 && value.data() == pointer &&
                      value.bytes()[15] == r::byte{42},
                  "failed reallocation preserves original buffer");
            stats->fail_after = 0;
            failed = false;
            try {
                r::shared_buffer copy{std::move(value)};
            } catch (const r::bad_alloc&) {
                failed = true;
            }
            check(failed && stats->live == 0,
                  "failed shared control block releases consumed storage");
        }
        stats->fail_after = -1;
        auto source = r::shared_buffer::copy({reinterpret_cast<const r::byte*>("payload"), 7});
        r::entry output{99, r::entry_type::barrier, source};
        stats->fail_after = 0;
        check(r::copy_entry({7, r::entry_type::command, source}, output) ==
                      r::errc::out_of_memory &&
                  output.term == 99 && output.payload.shares_storage_with(source),
              "resource failure preserves strong output contract");
        stats->fail_after = -1;
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite argument");
        const r::string_view suite = argv[1];
        if (suite == "memory_default")
            defaults();
        else if (suite == "memory_alignment")
            alignment();
        else if (suite == "memory_lifetime")
            lifetime();
        else if (suite == "memory_objects")
            objects();
        else if (suite == "memory_failure")
            failures();
        else if (suite == "memory_containers")
            containers();
        else
            check(false, "unknown suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
