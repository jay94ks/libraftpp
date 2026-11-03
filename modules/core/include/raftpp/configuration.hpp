#ifndef RAFTPP_CONFIGURATION_HPP
#define RAFTPP_CONFIGURATION_HPP

#include <raftpp/buffer.hpp>
#include <raftpp/error.hpp>
#include <raftpp/types.hpp>

namespace raftpp {

    struct server {
        server_id id;
        string address;
        raftpp::role role;
    };

    class RAFTPP_API configuration {
    public:
        [[nodiscard]] error_code add(server_id id, string_view address, role role) noexcept;
        [[nodiscard]] error_code remove(server_id id) noexcept;

        [[nodiscard]] size_t size() const noexcept {
            return servers_.size();
        }
        [[nodiscard]] bool empty() const noexcept {
            return servers_.empty();
        }
        [[nodiscard]] view<const server> servers() const noexcept {
            return servers_;
        }
        [[nodiscard]] borrowed<const server> get(server_id id) const noexcept;
        [[nodiscard]] size_t voter_count() const noexcept;

        // A missing index is represented by size(), matching the C implementation.
        [[nodiscard]] size_t index_of(server_id id) const noexcept;
        [[nodiscard]] size_t index_of_voter(server_id id) const noexcept;
        [[nodiscard]] size_t actual_index_of_voter(size_t index) const noexcept;

        // Version-1, little-endian encoding with zero padding to an 8-byte boundary.
        // encoded_size() returns zero if the size is not representable.
        [[nodiscard]] size_t encoded_size() const noexcept;
        [[nodiscard]] error_code encode(view<byte> output) const noexcept;
        [[nodiscard]] error_code encode(buffer& output) const noexcept;

        // On error the previous configuration is preserved. Views are invalidated
        // by successful mutations, as with the underlying standard vector.
        [[nodiscard]] error_code decode(view<const byte> input) noexcept;

    private:
        vector<server> servers_;
    };

}

#endif // RAFTPP_CONFIGURATION_HPP
