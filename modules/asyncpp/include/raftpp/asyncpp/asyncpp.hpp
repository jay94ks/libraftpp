#ifndef RAFTPP_ASYNCPP_HPP
#define RAFTPP_ASYNCPP_HPP

#include <raftpp/executor.hpp>
#include <raftpp/asyncpp/export.hpp>
#include <asyncpp/dispatcher.h>

namespace raftpp {
    class RAFTPP_ASYNCPP_API asyncpp_executor final : public executor {
    public:
        explicit asyncpp_executor(shared<asyncpp::dispatcher> dispatcher);
        error_code post(function<void()> callback) noexcept override;

    private:
        shared<asyncpp::dispatcher> dispatcher_;
    };
}
#endif
