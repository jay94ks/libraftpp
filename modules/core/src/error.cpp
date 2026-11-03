// Error codes and messages from cowsql/raft src/err.h; see LICENSE.md.
#include <raftpp/error.hpp>
#include <algorithm>
#include <cstdio>

namespace raftpp {
    namespace {

        class raft_error_category final : public error_category {
        public:
            const char* name() const noexcept override {
                return "raft";
            }

            string message(int value) const override {
                return string{error_message(static_cast<errc>(value))};
            }
        };

    }

    string_view error_message(errc value) noexcept {
        switch (value) {
        case errc::success:
            return "success";
        case errc::out_of_memory:
            return "out of memory";
        case errc::bad_id:
            return "server ID is not valid";
        case errc::duplicate_id:
            return "server ID already in use";
        case errc::duplicate_address:
            return "server address already in use";
        case errc::bad_role:
            return "server role is not valid";
        case errc::malformed:
            return "encoded data is malformed";
        case errc::not_leader:
            return "server is not the leader";
        case errc::leadership_lost:
            return "server has lost leadership";
        case errc::shutdown:
            return "server is shutting down";
        case errc::cannot_bootstrap:
            return "bootstrap only works on new clusters";
        case errc::cannot_change:
            return "a configuration change is already in progress";
        case errc::corrupt:
            return "persisted data is corrupted";
        case errc::canceled:
            return "operation canceled";
        case errc::name_too_long:
            return "resource name too long";
        case errc::too_big:
            return "data is too big";
        case errc::no_connection:
            return "no connection to remote server available";
        case errc::busy:
            return "operation can't be performed at this time";
        case errc::io_error:
            return "I/O error";
        case errc::not_found:
            return "Resource not found";
        case errc::invalid:
            return "Invalid parameter";
        case errc::unauthorized:
            return "No access to resource";
        case errc::no_space:
            return "Not enough disk space";
        case errc::too_many:
            return "System or raft limit met or exceeded";
        }
        return "unknown error";
    }

    const error_category& raft_category() noexcept {
        static const raft_error_category category;
        return category;
    }

    error_code make_error_code(errc value) noexcept {
        return {static_cast<int>(value), raft_category()};
    }

    string_view error_info::text() const noexcept {
        return {
            message.data(),
            static_cast<size_t>(std::find(message.begin(), message.end(), '\0') - message.begin())};
    }

    error_info wrap_error_info(error_info info, string_view prefix) noexcept {
        if (!info.code)
            return info;
        // Preserve C errMsgWrap's separator/truncation boundary.
        const auto previous = info.message;
        const auto count = std::min(prefix.size(), info.message.size() - 1);
        std::copy_n(prefix.data(), count, info.message.data());
        size_t used = count;
        if (prefix.size() < info.message.size() - 3) {
            info.message[used++] = ':';
            info.message[used++] = ' ';
            const auto length = static_cast<size_t>(
                std::find(previous.begin(), previous.end(), '\0') - previous.begin());
            const auto tail = std::min(length, info.message.size() - used - 1);
            std::copy_n(previous.data(), tail, info.message.data() + used);
            used += tail;
        }
        info.message[used] = '\0';
        return info;
    }

    error_info make_error_info(error_code code, string_view operation, string_view resource,
                               error_code cause) noexcept {
        if (!code)
            return {};
        error_info info{code, cause, {}};
        if (code.category() == raft_category()) {
            const auto text = error_message(static_cast<errc>(code.value()));
            std::copy_n(text.data(), std::min(text.size(), info.message.size() - 1),
                        info.message.data());
        } else
            std::snprintf(info.message.data(), info.message.size(), "%s:%d", code.category().name(),
                          code.value());
        if (cause) {
            const auto used = info.text().size();
            std::snprintf(info.message.data() + used, info.message.size() - used, " (%s:%d)",
                          cause.category().name(), cause.value());
        }
        if (!resource.empty())
            info = wrap_error_info(info, resource);
        return operation.empty() ? info : wrap_error_info(info, operation);
    }

}
