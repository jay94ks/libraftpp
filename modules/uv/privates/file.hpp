#ifndef RAFTPP_DETAIL_FILE_HPP
#define RAFTPP_DETAIL_FILE_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/error.hpp>
#include <raftpp/config.hpp>

#if RAFTPP_WITH_UV
namespace raftpp::detail {

    // C++20 has no owning native file descriptor type.
    class RAFTPP_UV_API file_handle {
    public:
        file_handle() noexcept = default;
        explicit file_handle(int descriptor) noexcept : descriptor_(descriptor) {}
        ~file_handle();
        file_handle(file_handle&& other) noexcept : descriptor_(other.release()) {}
        file_handle& operator=(file_handle&& other) noexcept;
        file_handle(const file_handle&) = delete;
        file_handle& operator=(const file_handle&) = delete;
        [[nodiscard]] int get() const noexcept {
            return descriptor_;
        }
        [[nodiscard]] int release() noexcept {
            return std::exchange(descriptor_, -1);
        }
        [[nodiscard]] error_code close(error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code allocate(size_t size, error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] static error_code open(const fs::path& path, int flags, int mode,
                                             file_handle& output,
                                             error_info* diagnostic = nullptr) noexcept;

    private:
        int descriptor_ = -1;
    };

}
#endif
#endif // RAFTPP_DETAIL_FILE_HPP
