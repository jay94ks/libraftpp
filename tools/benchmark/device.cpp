#include "device.hpp"
#include <cerrno>
#include <charconv>
#include <fstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace raftpp::benchmark {
    namespace {
        string read_line(const fs::path& path) {
            std::ifstream input{path};
            if (!input)
                throw system_error(errno ? errno : EIO, std::generic_category(),
                                   "read device info " + path.string());
            string line;
            if (!std::getline(input, line))
                throw std::runtime_error("empty device info " + path.string());
            return line;
        }
        uint64_t read_number(const fs::path& path) {
            const auto text = read_line(path);
            uint64_t value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::runtime_error("invalid device sector count " + path.string());
            return value;
        }
    }
    device_info inspect_device(const fs::path& path, const fs::path& sysfs) {
        struct stat info {};
        if (stat(path.c_str(), &info) < 0)
            throw system_error(errno, std::generic_category(),
                               "stat benchmark target " + path.string());
        device_info result;
        result.raw = S_ISBLK(info.st_mode);
        result.device = result.raw ? info.st_rdev : info.st_dev;
        result.parent_device = result.device;
        const auto link = sysfs / (std::to_string(major(result.device)) + ":" +
                                   std::to_string(minor(result.device)));
        error_code error;
        const auto device = fs::canonical(link, error);
        if (error == std::errc::no_such_file_or_directory)
            return result; // e.g. overlayfs/tmpfs.
        if (error)
            throw system_error(error, "resolve block device " + link.string());
        const auto start = device / "start";
        if (fs::exists(start))
            result.first_sector = read_number(start);
        const auto sectors = read_number(device / "size");
        if (sectors > numeric_limits<uint64_t>::max() - result.first_sector)
            throw std::runtime_error("device sector range overflow");
        result.end_sector = result.first_sector + sectors;
        const auto whole = fs::exists(device / "partition") ? device.parent_path() : device;
        result.disk_name = whole.filename().string();
        if (fs::exists(whole / "dev")) {
            const auto number = read_line(whole / "dev");
            const auto colon = number.find(':');
            unsigned maj = 0, min = 0;
            if (colon == string::npos)
                throw std::runtime_error("invalid parent device number");
            const auto first = std::from_chars(number.data(), number.data() + colon, maj);
            const auto last =
                std::from_chars(number.data() + colon + 1, number.data() + number.size(), min);
            if (first.ec != std::errc{} || first.ptr != number.data() + colon ||
                last.ec != std::errc{} || last.ptr != number.data() + number.size())
                throw std::runtime_error("invalid parent device number");
            result.parent_device = makedev(maj, min);
        }
        if (fs::exists(whole / "queue/logical_block_size"))
            result.logical_block_size = read_number(whole / "queue/logical_block_size");
        const auto name = device.filename().string();
        if (name.starts_with("nvme"))
            result.driver = device_driver::nvme;
        else if (name.starts_with("nullb"))
            result.driver = device_driver::null_block;
        if (result.driver != device_driver::generic) {
            auto cache = device / "queue/write_cache";
            if (!fs::exists(cache))
                cache = device.parent_path() / "queue/write_cache";
            const auto policy = read_line(cache);
            if (policy != "write through" && policy != "write back")
                throw std::runtime_error("invalid device write-cache policy " + cache.string());
            result.write_through = policy == "write through";
        }
        return result;
    }
}
