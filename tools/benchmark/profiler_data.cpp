#include "profiler_data.hpp"
#include <bit>
#include <charconv>
#include <cstring>
#include <sstream>
#include <sys/sysmacros.h>
#if RAFTPP_BENCHMARK_HAS_PERF
#include <linux/perf_event.h>
#endif

namespace raftpp::benchmark {
    namespace {
        string_view trim(string_view text) {
            while (!text.empty() &&
                   (text.front() == ' ' || text.front() == '\t' || text.front() == '\n'))
                text.remove_prefix(1);
            while (!text.empty() &&
                   (text.back() == ' ' || text.back() == '\t' || text.back() == '\n'))
                text.remove_suffix(1);
            return text;
        }
        uint64_t integer(string_view text) {
            text = trim(text);
            uint64_t value = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
                throw invalid_argument("invalid kernel metadata number");
            return value;
        }
        uint64_t native_number(view<const byte> bytes) {
            switch (bytes.size()) {
            case 1:
                return std::to_integer<unsigned>(bytes[0]);
            case 2: {
                uint16_t value;
                std::memcpy(&value, bytes.data(), 2);
                return value;
            }
            case 4: {
                uint32_t value;
                std::memcpy(&value, bytes.data(), 4);
                return value;
            }
            case 8: {
                uint64_t value;
                std::memcpy(&value, bytes.data(), 8);
                return value;
            }
            default:
                throw invalid_argument("unsupported tracepoint integer width");
            }
        }
        uint64_t little(view<const byte> bytes) {
            uint64_t value = 0;
            for (size_t i = 0; i < bytes.size(); ++i)
                value |= static_cast<uint64_t>(std::to_integer<unsigned>(bytes[i])) << (i * 8);
            return value;
        }
        uint64_t kernel_device(dev_t device) {
            return (static_cast<uint64_t>(major(device)) << 20) | minor(device);
        }
    }
    trace_format trace_format::parse(string_view text) {
        trace_format result;
        while (!text.empty()) {
            const auto end = text.find('\n');
            auto line = trim(text.substr(0, end));
            if (end == string_view::npos)
                text = {};
            else
                text.remove_prefix(end + 1);
            if (line.starts_with("ID:")) {
                const auto id = integer(line.substr(3));
                if (id == 0 || id > numeric_limits<uint16_t>::max() || result.id_)
                    throw invalid_argument("invalid tracepoint ID");
                result.id_ = static_cast<unsigned>(id);
            } else if (line.starts_with("field:")) {
                const auto semicolon = line.find(';');
                if (semicolon == string_view::npos)
                    throw invalid_argument("invalid tracepoint field");
                auto declaration = trim(line.substr(6, semicolon - 6));
                const bool dynamic = declaration.find("__data_loc") != string_view::npos;
                auto name = declaration.substr(declaration.find_last_of(" \t") + 1);
                if (const auto array = name.find('['); array != string_view::npos)
                    name = name.substr(0, array);
                if (!name.empty() && name.front() == '*')
                    name.remove_prefix(1);
                auto member = [&](string_view key) {
                    const auto start = line.find(key, semicolon);
                    if (start == string_view::npos)
                        throw invalid_argument("missing tracepoint field metadata");
                    auto number = line.substr(start + key.size());
                    return integer(number.substr(0, number.find(';')));
                };
                const auto offset = member("offset:"), size = member("size:");
                if (name.empty() || size == 0 || offset > 65535 || size > 65535 ||
                    offset + size > 65535 || (dynamic && size != 4))
                    throw invalid_argument("invalid tracepoint field bounds");
                if (!result.fields_
                         .emplace(string{name}, trace_field{static_cast<size_t>(offset),
                                                            static_cast<size_t>(size), dynamic})
                         .second)
                    throw invalid_argument("duplicate tracepoint field");
            }
        }
        if (!result.id_ || !result.fields_.contains("common_type"))
            throw invalid_argument("incomplete tracepoint format");
        return result;
    }
    view<const byte> trace_format::bytes(view<const byte> raw, string_view name) const {
        const auto found = fields_.find(name);
        if (found == fields_.end())
            throw invalid_argument("missing tracepoint field " + string{name});
        const auto& field = found->second;
        if (field.offset > raw.size() || field.size > raw.size() - field.offset)
            throw invalid_argument("truncated tracepoint field");
        auto result = raw.subspan(field.offset, field.size);
        if (field.dynamic) {
            const auto location = native_number(result);
            const auto offset = static_cast<size_t>(location & 65535),
                       size = static_cast<size_t>(location >> 16);
            if (offset > raw.size() || size > raw.size() - offset)
                throw invalid_argument("truncated dynamic tracepoint field");
            result = raw.subspan(offset, size);
        }
        return result;
    }
    uint64_t trace_format::number(view<const byte> raw, string_view name) const {
        return native_number(bytes(raw, name));
    }
    string trace_format::text(view<const byte> raw, string_view name) const {
        const auto value = bytes(raw, name);
        const auto end = std::find(value.begin(), value.end(), byte{0});
        return string{reinterpret_cast<const char*>(value.data()),
                      static_cast<size_t>(end - value.begin())};
    }
    vector<int> parse_cpu_list(string_view text) {
        text = trim(text);
        vector<int> result;
        while (!text.empty()) {
            const auto comma = text.find(',');
            const auto range = text.substr(0, comma);
            const auto dash = range.find('-');
            const auto first = integer(range.substr(0, dash));
            const auto last = dash == string_view::npos ? first : integer(range.substr(dash + 1));
            if (last < first || last > static_cast<uint64_t>(numeric_limits<int>::max()) ||
                last - first >= 1048576)
                throw invalid_argument("invalid online CPU range");
            for (auto cpu = first; cpu <= last; ++cpu)
                result.push_back(static_cast<int>(cpu));
            if (comma == string_view::npos)
                break;
            text.remove_prefix(comma + 1);
            if (text.empty())
                throw invalid_argument("empty online CPU range");
        }
        if (result.empty())
            throw invalid_argument("no online CPUs");
        std::sort(result.begin(), result.end());
        if (std::adjacent_find(result.begin(), result.end()) != result.end())
            throw invalid_argument("duplicate online CPU");
        return result;
    }
    vector<perf_sample> read_perf_ring(view<byte> mapping, size_t page_size) {
#if RAFTPP_BENCHMARK_HAS_PERF
        static_assert(atomic_ref<decltype(perf_event_mmap_page::data_head)>::is_always_lock_free);
        if (mapping.size() < sizeof(perf_event_mmap_page) ||
            page_size < sizeof(perf_event_mmap_page) ||
            reinterpret_cast<uintptr_t>(mapping.data()) % alignof(perf_event_mmap_page))
            throw invalid_argument("invalid perf metadata mapping");
        auto& control = *reinterpret_cast<perf_event_mmap_page*>(mapping.data());
        const auto offset = control.data_offset ? control.data_offset : page_size;
        const auto size = control.data_size ? control.data_size
                                            : mapping.size() - std::min(page_size, mapping.size());
        if (offset < page_size || offset > mapping.size() || size > mapping.size() - offset ||
            !std::has_single_bit(size))
            throw invalid_argument("invalid perf ring bounds");
        const auto head = atomic_ref{control.data_head}.load(std::memory_order_acquire);
        auto tail = control.data_tail;
        if (head - tail > size)
            throw std::runtime_error("perf ring overflow: measurements lost");
        const auto ring = mapping.subspan(static_cast<size_t>(offset), static_cast<size_t>(size));
        auto copy = [&](uint64_t position, view<byte> output) {
            const auto begin = static_cast<size_t>(position & (size - 1));
            const auto first = std::min(output.size(), ring.size() - begin);
            std::memcpy(output.data(), ring.data() + begin, first);
            if (first != output.size())
                std::memcpy(output.data() + first, ring.data(), output.size() - first);
        };
        vector<perf_sample> samples;
        while (tail != head) {
            perf_event_header header{};
            if (head - tail < sizeof header)
                throw std::runtime_error("incomplete perf record header");
            copy(tail, {reinterpret_cast<byte*>(&header), sizeof header});
            if (header.size < sizeof header || header.size > size || header.size > head - tail)
                throw std::runtime_error("invalid perf record length");
            if (header.type != PERF_RECORD_SAMPLE)
                throw std::runtime_error("lost, throttled, or unexpected perf record");
            vector<byte> record(header.size);
            copy(tail, record);
            if (record.size() < 28)
                throw std::runtime_error("truncated perf sample");
            perf_sample sample;
            sample.time = native_number(view<const byte>{record}.subspan(8, 8));
            sample.cpu =
                static_cast<unsigned>(native_number(view<const byte>{record}.subspan(16, 4)));
            const auto raw_size = native_number(view<const byte>{record}.subspan(24, 4));
            if (raw_size < 2 || raw_size > record.size() - 28)
                throw std::runtime_error("invalid raw perf sample length");
            sample.raw.assign(record.begin() + 28,
                              record.begin() + 28 + static_cast<ptrdiff_t>(raw_size));
            samples.push_back(std::move(sample));
            tail += header.size;
        }
        atomic_ref{control.data_tail}.store(tail, std::memory_order_release);
        return samples;
#else
        (void)mapping;
        (void)page_size;
        throw system_error(std::make_error_code(std::errc::operation_not_supported),
                           "perf headers unavailable at build time");
#endif
    }
    kernel_measurements correlate(vector<perf_sample> samples, view<const event_format> formats,
                                  const device_info& device, uint64_t expected, unsigned issuer) {
        std::stable_sort(samples.begin(), samples.end(),
                         [](const auto& a, const auto& b) { return a.time < b.time; });
        struct pending {
            uint64_t start, sectors;
        };
        map<uint64_t, pending> blocks, nvme;
        kernel_measurements result;
        auto sector = [&](uint64_t dev, uint64_t value) -> optional<uint64_t> {
            if (dev == kernel_device(device.device)) {
                if (device.parent_device != device.device) {
                    if (value > numeric_limits<uint64_t>::max() - device.first_sector)
                        throw std::runtime_error("block sector overflow");
                    value += device.first_sector;
                }
            } else if (dev != kernel_device(device.parent_device))
                return {};
            if (value < device.first_sector || value >= device.end_sector)
                return {};
            return value;
        };
        for (const auto& sample : samples) {
            if (sample.raw.size() < 2)
                throw invalid_argument("truncated tracepoint sample");
            const auto id = native_number(view<const byte>{sample.raw}.first(2));
            const auto found = std::find_if(formats.begin(), formats.end(), [&](const auto& value) {
                return value.format.id() == id;
            });
            if (found == formats.end())
                throw invalid_argument("unknown perf tracepoint ID");
            const auto& f = found->format;
            const auto raw = view<const byte>{sample.raw};
            if (f.number(raw, "common_type") != id)
                throw invalid_argument("mismatched tracepoint type");
            const bool start =
                found->kind == kernel_event::block_queue || found->kind == kernel_event::nvme_setup;
            if (start && f.number(raw, "common_pid") != issuer)
                continue;
            const bool block = found->kind == kernel_event::block_queue ||
                               found->kind == kernel_event::block_request_complete ||
                               found->kind == kernel_event::block_bio_complete;
            uint64_t key, sectors = 0;
            if (block) {
                const auto where = sector(f.number(raw, "dev"), f.number(raw, "sector"));
                if (!where)
                    continue;
                if (f.text(raw, "rwbs").find('W') == string::npos)
                    continue;
                key = *where;
                sectors = f.number(raw, "nr_sector");
                if (!sectors || sectors > device.end_sector - key)
                    throw std::runtime_error("invalid block command range");
            } else {
                if (f.text(raw, "disk") != device.disk_name)
                    continue;
                const auto controller = f.number(raw, "ctrl_id"), queue = f.number(raw, "qid"),
                           command = f.number(raw, "cid");
                if (controller > numeric_limits<uint32_t>::max() || queue > 65535 ||
                    command > 65535)
                    throw std::runtime_error("invalid NVMe command key");
                key = (controller << 32) | (queue << 16) | command;
                if (start) {
                    if (f.number(raw, "opcode") != 1)
                        continue;
                    const auto data = f.bytes(raw, "cdw10");
                    if (data.size() < 12 || device.logical_block_size < 512 ||
                        device.logical_block_size % 512)
                        throw std::runtime_error("invalid NVMe LBA metadata");
                    const auto lba = little(data.first(8)), scale = device.logical_block_size / 512;
                    if (lba > numeric_limits<uint64_t>::max() / scale)
                        throw std::runtime_error("NVMe LBA overflow");
                    const auto begin = lba * scale;
                    if (begin < device.first_sector || begin >= device.end_sector)
                        continue;
                    sectors = (little(data.subspan(8, 2)) + 1) * scale;
                    if (sectors > device.end_sector - begin)
                        throw std::runtime_error("NVMe command exceeds device range");
                    if (little(data.subspan(10, 2)) != (device.write_through ? 0U : 0x4000U))
                        throw std::runtime_error("unexpected NVMe durable-write control flags");
                }
            }
            auto& commands = block ? blocks : nvme;
            auto& latencies = block ? result.block : result.nvme;
            if (start) {
                if (!commands.emplace(key, pending{sample.time, sectors}).second)
                    throw std::runtime_error("overlapping kernel command key");
            } else {
                const auto command = commands.find(key);
                if (command == commands.end())
                    continue; // Other I/O or duplicate rq/bio completion.
                if (f.number(raw, block ? "error" : "status"))
                    throw std::runtime_error("kernel command completion failed");
                if (sample.time <= command->second.start)
                    throw std::runtime_error("nonpositive kernel command duration");
                if (block && sectors != command->second.sectors)
                    throw std::runtime_error("split or merged block command");
                latencies.add(sample.time - command->second.start);
                commands.erase(command);
            }
        }
        if (!blocks.empty() || !nvme.empty() || result.block.size() != expected ||
            (device.driver == device_driver::nvme && result.nvme.size() != expected))
            throw std::runtime_error("incomplete or ambiguous kernel measurements");
        return result;
    }
}
