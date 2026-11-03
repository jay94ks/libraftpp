// C++ port of cowsql/raft src/message.c; see LICENSE.md.
#include <raftpp/detail/protocol.hpp>

namespace raftpp::detail {

    error_code enqueue_message(core_update& update, server_id id, string_view address,
                               message_payload payload) noexcept {
        try {
            update.messages.push_back({id, string{address}, std::move(payload)});
            update.flags |= update_messages;
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

}
