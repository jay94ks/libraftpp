#include <raftpp.hpp>
#include <raftpp/detail/log.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <source_location>

namespace r = raftpp;

namespace {

    void check(bool condition, r::string_view message,
               std::source_location location = std::source_location::current()) {
        if (!condition) {
            throw std::runtime_error(r::string{location.file_name()} + ":" +
                                     std::to_string(location.line()) + ": " + r::string{message});
        }
    }

    r::view<const r::byte> bytes(r::string_view text) {
        return {reinterpret_cast<const r::byte*>(text.data()), text.size()};
    }

    void test_shared_buffer() {
        r::weak<const r::byte> lifetime;
        r::shared_buffer retained;
        {
            r::aligned_buffer aligned{123, 4096};
            check(reinterpret_cast<r::uintptr_t>(aligned.data()) % 4096 == 0, "aligned address");
            check(std::ranges::all_of(aligned.bytes(),
                                      [](auto value) { return value == r::byte{0}; }),
                  "aligned storage is zero initialized");
            aligned.bytes()[122] = r::byte{42};
            r::shared_buffer original{std::move(aligned)};
            check(aligned.empty(), "moved-from aligned buffer is empty");
            retained = original.slice(100, 23);
            check(retained.shares_storage_with(original), "slice retains allocation identity");
            lifetime = original.storage();
            auto moved = std::move(original);
            check(original.empty() && original.bytes().empty(),
                  "moved-from shared buffer is empty");
            check(moved.size() == 123, "moving a shared buffer retains length");
        }
        check(!lifetime.expired() && retained.bytes().back() == r::byte{42},
              "slice survives owner scope");
        retained = {};
        check(lifetime.expired(), "last shared slice releases aligned allocation");

        auto empty = r::shared_buffer::copy({});
        check(empty.slice(0).empty() && empty.shares_storage_with(empty.slice(0)),
              "empty allocation can be shared without null-pointer arithmetic");
        bool rejected = false;
        try {
            (void)empty.slice(1);
        } catch (const r::out_of_range&) {
            rejected = true;
        }
        check(rejected, "invalid slice offset is rejected");
        auto value = r::shared_buffer::copy(bytes("abc"));
        rejected = false;
        try {
            (void)value.slice(2, 2);
        } catch (const r::out_of_range&) {
            rejected = true;
        }
        check(rejected, "invalid slice length is rejected");
        rejected = false;
        try {
            r::aligned_buffer invalid{1, 3};
        } catch (const r::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "non-power-of-two alignment is rejected");
        r::aligned_buffer first{8, 64};
        r::aligned_buffer second{12, 512};
        first.swap(second);
        check(first.size() == 12 && first.alignment() == 512 && second.size() == 8 &&
                  second.alignment() == 64,
              "swap moves matching deleters");
        second = std::move(first);
        check(first.empty() && second.alignment() == 512, "move assignment retains alignment");
    }

    void test_entries() {
        r::vector<r::entry> source{
            {1, r::entry_type::command, r::shared_buffer::copy(bytes("one"))},
            {2, r::entry_type::barrier,
             r::shared_buffer::copy(bytes(r::string_view{"two\0three", 9}))},
            {3, r::entry_type::change, r::shared_buffer::copy(bytes("four"))}};
        r::vector<r::entry> copied;
        check(!r::copy_entries(source, copied), "copy entries into batch");
        for (r::size_t i = 0; i < source.size(); ++i) {
            check(copied[i].term == source[i].term && copied[i].type == source[i].type,
                  "batch metadata matches expected values");
            check(std::ranges::equal(copied[i].payload.bytes(), source[i].payload.bytes()),
                  "batch bytes match C");
            check(copied[i].payload.shares_storage_with(copied[0].payload), "one batch allocation");
            check(!copied[i].payload.shares_storage_with(source[i].payload),
                  "deep copy owns separate data");
        }
        check(copied[1].payload.bytes().data() ==
                  copied[0].payload.bytes().data() + copied[0].payload.size(),
              "batch copy uses original unpadded layout");
        r::entry single;
        check(!r::copy_entry(source[0], single), "deep copy one entry");
        check(std::ranges::equal(single.payload.bytes(), source[0].payload.bytes()) &&
                  !single.payload.shares_storage_with(source[0].payload),
              "single entry is an independent deep copy");
        check(!r::copy_entry(single, single), "entry copy supports self aliasing");
        check(!r::copy_entries(copied, copied), "batch copy supports input/output aliasing");
        check(copied.size() == 3 && copied[1].payload.size() == 9, "aliased batch retains content");

        r::weak<const r::byte> lifetime = copied[0].payload.storage();
        auto last = copied[1].payload;
        copied.clear();
        check(!lifetime.expired() && last.size() == 9, "individual entry retains whole batch");
        last = {};
        check(lifetime.expired(), "last batch entry releases storage");
        r::array<r::entry, 2> empty{};
        check(!r::copy_entries(empty, copied) && copied.size() == 2 &&
                  copied[0].payload.shares_storage_with(copied[1].payload),
              "empty payload batch");
        check(!r::copy_entries({}, copied) && copied.empty(), "empty input clears output");
    }

    // A sparse value model is independent of the log's deque/weak-header cache.
    struct log_model {
        r::map<r::log_index, r::entry> entries;
        r::map<r::pair<r::term, r::log_index>, r::weak<const r::entry>> retained;
        r::log_index offset = 0;
        struct snapshot_state {
            r::log_index last_index = 0;
            r::term term = 0;
        } snapshot;
        void start(r::log_index index, r::term term, r::log_index first) {
            snapshot = {index, term};
            offset = first - 1;
        }
        r::log_index last() const {
            return entries.empty() ? offset : entries.rbegin()->first;
        }
        r::term term_of(r::log_index index) const {
            if (index == 0 || index > last())
                return 0;
            if (index == snapshot.last_index)
                return snapshot.term;
            const auto found = entries.find(index);
            return found == entries.end() ? 0 : found->second.term;
        }

        void truncate(r::log_index index) {
            entries.erase(entries.lower_bound(index), entries.end());
        }
        void restore(r::log_index index, r::term term) {
            entries.clear();
            offset = index;
            snapshot = {index, term};
        }
        void compact(r::log_index index, unsigned trailing) {
            const auto term = term_of(index);
            snapshot = {index, term};
            if (index > trailing && entries.contains(index - trailing)) {
                offset = index - trailing;
                entries.erase(entries.begin(), entries.upper_bound(offset));
            }
        }
    };
    struct log_fixture {
        r::owned<log_model> value = std::make_unique<log_model>();
    };
    struct acquired {
        r::vector<r::shared<const r::entry>> cpp;
        r::vector<r::entry> values;
    };
    void compare_entry(const r::entry& actual, const r::entry& expected) {
        check(actual.term == expected.term && actual.type == expected.type &&
                  std::ranges::equal(actual.payload.bytes(), expected.payload.bytes()),
              "log entry retains submitted term/type/bytes");
    }
    r::owned<acquired> acquire(r::detail::log& actual, log_model* expected, r::log_index index,
                               int maximum) {
        auto result = std::make_unique<acquired>();
        const auto limit =
            maximum < 0 ? r::numeric_limits<r::size_t>::max() : static_cast<r::size_t>(maximum);
        check(!actual.acquire(index, result->cpp, limit), "acquire log entries");
        auto it = expected->entries.find(index);
        while (it != expected->entries.end() && result->values.size() < limit) {
            result->values.push_back(it->second);
            ++it;
        }
        check(result->cpp.size() == result->values.size(),
              "acquisition respects range and maximum");
        for (r::size_t i = 0; i < result->cpp.size(); ++i)
            expected->retained[{result->cpp[i]->term, index + i}] = result->cpp[i];
        return result;
    }
    void compare_log(const r::detail::log& actual, const log_model* expected) {
        check(actual.size() == expected->entries.size() &&
                  actual.last_index() == expected->last() &&
                  actual.last_term() == expected->term_of(expected->last()),
              "log bounds match independent sparse model");
        check(actual.snapshot_index() == expected->snapshot.last_index &&
                  actual.snapshot_term() == expected->snapshot.term,
              "snapshot metadata matches model");
        for (auto index = expected->offset > 1 ? expected->offset - 1 : 1;
             index <= actual.last_index() + 1; ++index) {
            const auto got = actual.get(index);
            const auto found = expected->entries.find(index);
            check((got != nullptr) == (found != expected->entries.end()) &&
                      actual.term_of(index) == expected->term_of(index),
                  "term and presence lookup match model");
            if (got)
                compare_entry(*got, found->second);
        }
    }
    void append(r::detail::log& actual, log_model* expected, r::term term, r::entry_type type,
                r::string_view text) {
        const auto index = expected->last() + 1;
        const bool busy = !expected->retained[{term, index}].expired();
        r::entry value{term, type, r::shared_buffer::copy(bytes(text))};
        const auto status = actual.append(value);
        check(status == (busy ? r::make_error_code(r::errc::busy) : r::error_code{}),
              "retained term/index blocks reuse until completion");
        if (!status)
            expected->entries.emplace(index, std::move(value));
    }

    void test_log() {
        r::detail::log actual;
        log_fixture reference;
        auto expected = reference.value.get();
        check(!actual.start(3, 4, 2), "start log replay");
        expected->start(3, 4, 2);
        compare_log(actual, expected);
        for (const auto term : {3, 4, 4}) {
            append(actual, expected, term, r::entry_type::command, "replay");
            compare_log(actual, expected);
        }

        std::mt19937 random{52711};
        r::vector<r::owned<acquired>> held;
        for (unsigned step = 0; step < 6000; ++step) {
            const auto action = random() % 10;
            if (action < 5) {
                const auto term = std::max<r::term>(1, actual.last_term()) + random() % 2;
                append(actual, expected, term, static_cast<r::entry_type>(1 + random() % 3),
                       "entry-" + std::to_string(step));
            } else if (action == 5) {
                const auto first =
                    std::max(expected->offset + 1, expected->snapshot.last_index + 1);
                if (first <= actual.last_index()) {
                    const auto index = first + random() % (actual.last_index() - first + 1);
                    check(!actual.truncate(index), "truncate log suffix");
                    expected->truncate(index);
                }
            } else if (action == 6) {
                const auto first =
                    std::max(expected->offset + 1, expected->snapshot.last_index + 1);
                if (first <= actual.last_index()) {
                    const auto index = first + random() % (actual.last_index() - first + 1);
                    const auto trailing = random() % 8;
                    check(!actual.snapshot(index, trailing), "snapshot log prefix");
                    expected->compact(index, trailing);
                }
            } else if (action == 7) {
                const auto index = actual.last_index() + 1 + random() % 5;
                const auto term = actual.last_term() + 1;
                check(!actual.restore(index, term), "restore log snapshot");
                expected->restore(index, term);
            } else if (action == 8 && held.size() < 16) {
                const auto index = expected->offset + 1;
                const auto maximum = random() % 2 == 0 ? -1 : static_cast<int>(random() % 5);
                held.push_back(acquire(actual, expected, index, maximum));
            } else if (!held.empty()) {
                held.erase(held.begin() + static_cast<r::ptrdiff_t>(random() % held.size()));
            }
            compare_log(actual, expected);
            for (const auto& request : held) {
                for (r::size_t i = 0; i < request->cpp.size(); ++i) {
                    compare_entry(*request->cpp[i], request->values[i]);
                }
            }
        }
        held.clear();

        // Deterministically preserve the original per-(term,index) busy contract.
        check(!actual.restore(10000, 1), "reset log for busy test");
        expected->restore(10000, 1);
        append(actual, expected, 2, r::entry_type::command, "old");
        auto old = acquire(actual, expected, 10001, -1);
        check(!actual.truncate(10001), "truncate retained entry");
        expected->truncate(10001);
        append(actual, expected, 2, r::entry_type::command, "busy");
        check(actual.empty(), "same term/index remains busy while acquired");
        append(actual, expected, 3, r::entry_type::command, "new-term");
        auto newer = acquire(actual, expected, 10001, -1);
        check(!actual.truncate(10001), "truncate replacement term");
        expected->truncate(10001);
        append(actual, expected, 2, r::entry_type::command, "still-busy");
        old.reset();
        append(actual, expected, 2, r::entry_type::command, "retry");
        newer.reset();
        compare_log(actual, expected);

        r::configuration configuration;
        check(!configuration.add(1, "node-one", r::role::voter), "create log configuration");
        check(!actual.append_configuration(4, configuration), "append configuration entry");
        r::buffer encoded;
        check(!configuration.encode(encoded), "encode expected configuration");
        expected->entries.emplace(
            expected->last() + 1,
            r::entry{4, r::entry_type::change, r::shared_buffer{std::move(encoded)}});
        compare_log(actual, expected);

        r::array<r::shared_buffer, 2> commands{r::shared_buffer::copy(bytes("first")),
                                               r::shared_buffer::copy(bytes("second"))};
        const auto start = actual.last_index() + 1;
        check(!actual.append_commands(5, commands), "append command range");
        check(actual.size() >= 2 && actual.get(start + 1)->type == r::entry_type::command,
              "command range metadata");
        check(!actual.discard(start), "rollback command range");
        compare_log(actual, expected);
        check(!actual.restore(r::numeric_limits<r::log_index>::max(), 1),
              "restore maximum log index");
        check(actual.append({1, r::entry_type::command, {}}) == r::errc::too_big,
              "reject index overflow");
    }

    void test_log_lifetime() {
        r::vector<r::shared<const r::entry>> held;
        r::weak<const r::byte> lifetime;
        {
            r::detail::log log;
            r::array<r::entry, 3> source{
                r::entry{1, r::entry_type::command, r::shared_buffer::copy(bytes("one"))},
                r::entry{1, r::entry_type::command, r::shared_buffer::copy(bytes("two"))},
                r::entry{1, r::entry_type::command, r::shared_buffer::copy(bytes("three"))}};
            r::vector<r::entry> batch;
            check(!r::copy_entries(source, batch), "create shared log batch");
            lifetime = batch[0].payload.storage();
            for (const auto& entry : batch)
                check(!log.append(entry), "append shared batch entry");
            batch.clear();
            check(!log.acquire(2, held, 1), "retain one entry of batch");
            check(!log.restore(10, 2), "restore while request retains old entry");
            check(!lifetime.expired(), "request owns complete retired batch");
        }
        check(!lifetime.expired() && std::ranges::equal(held[0]->payload.bytes(), bytes("two")),
              "acquisition survives log destruction");
        held.clear();
        check(lifetime.expired(),
              "completion releases batch without calling back into destroyed log");
    }

    void test_snapshot() {
        r::snapshot input;
        input.index = 20;
        input.term = 4;
        input.configuration_index = 10;
        check(!input.configuration.add(7, "snapshot-node", r::role::voter),
              "snapshot configuration");
        input.buffers = {r::shared_buffer::copy(bytes("one")),
                         r::shared_buffer::copy(bytes("two"))};
        r::snapshot output;
        check(!r::copy_snapshot(input, output), "deep snapshot copy");
        check(output.index == 20 && output.term == 4 && output.configuration_index == 10,
              "snapshot metadata survives copying");
        check(output.buffers.size() == 1 &&
                  std::ranges::equal(output.buffers[0].bytes(), bytes("onetwo")),
              "snapshot copy concatenates buffers in order");
        check(!output.buffers[0].shares_storage_with(input.buffers[0]),
              "snapshot copy has independent storage");
        check(!input.configuration.remove(7) && output.configuration.get(7) != nullptr,
              "snapshot configuration is deeply copied");
        check(!r::copy_snapshot(output, output) && output.buffers[0].size() == 6,
              "snapshot self copy");
        r::snapshot empty;
        check(!r::copy_snapshot(empty, output) && output.buffers.size() == 1 &&
                  output.buffers[0].empty(),
              "empty snapshot is compacted safely");
    }

}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected a storage test suite");
        const r::string_view suite{argv[1]};
        if (suite == "shared_buffer")
            test_shared_buffer();
        else if (suite == "entries")
            test_entries();
        else if (suite == "log")
            test_log();
        else if (suite == "log_lifetime")
            test_log_lifetime();
        else if (suite == "snapshot")
            test_snapshot();
        else
            throw std::runtime_error("unknown storage test suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
