#ifndef RAFTPP_SRC_UV_ERROR_HPP
#define RAFTPP_SRC_UV_ERROR_HPP
#include <raftpp/error.hpp>
#include <netdb.h>
#include <uv.h>
namespace raftpp::detail {
    class uv_error_category final : public error_category {
    public:
        const char* name() const noexcept override {
            return "libuv";
        }
        string message(int value) const override {
            return uv_strerror(value);
        }
        error_condition default_error_condition(int value) const noexcept override {
            if (value < 0 && value > UV_EAI_ADDRFAMILY)
                return {-value, std::generic_category()};
            return {value, *this};
        }
    };
    class resolver_error_category final : public error_category {
    public:
        const char* name() const noexcept override {
            return "getaddrinfo";
        }
        string message(int value) const override {
            return gai_strerror(value);
        }
    };
    inline error_code uv_cause(int value) noexcept {
        static const uv_error_category category;
        return value ? error_code{value, category} : error_code{};
    }
    inline error_code resolver_cause(int value, int system_error) noexcept {
        static const resolver_error_category category;
        return value == EAI_SYSTEM ? error_code{system_error, std::generic_category()}
                                   : error_code{value, category};
    }
    inline error_code native_error(int status) noexcept {
        return status == UV_ENOMEM ? errc::out_of_memory : errc::io_error;
    }
}
#endif // RAFTPP_SRC_UV_ERROR_HPP
