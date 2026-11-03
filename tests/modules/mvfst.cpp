#include <raftpp/mvfst/mvfst.hpp>
#include "mvfst_socket_stub.hpp"
#include <cstring>

int main() {
    using namespace raftpp;
    auto connection = std::make_shared<quic::raftpp_test_socket>();
    std::weak_ptr<quic::raftpp_test_socket> lifetime = connection;
    auto stream = std::make_unique<mvfst_stream>(connection, 0);
    byte_stream& base = *stream;
    const array<byte, 3> bytes{byte{1}, byte{2}, byte{3}};
    int written = 0, reads = 0, eof = 0, closed = 0;
    if (base.write(shared_buffer::copy(bytes),
                   [&](error_code code) {
                       if (!code)
                           ++written;
                   }) ||
        written != 1 || !connection->written || connection->written->computeChainDataLength() != 3)
        return 1;
    connection->fail_write = true;
    if (!base.write(shared_buffer::copy(bytes), [&](error_code) { ++written; }) || written != 1)
        return 1;
    if (base.read_start([&](error_code error, shared_buffer value) {
            if (error == errc::no_connection)
                ++eof;
            else if (!error && value.size() == 3 &&
                     std::memcmp(value.bytes().data(), bytes.data(), 3) == 0)
                ++reads;
        }))
        return 1;
    connection->incoming = folly::IOBuf::copyBuffer(bytes.data(), bytes.size());
    connection->callback->readAvailable(0);
    if (reads != 1 || base.read_stop() || !connection->paused)
        return 1;
    if (base.read_start([&](error_code error, shared_buffer) {
            if (error == errc::no_connection)
                ++eof;
        }) ||
        connection->paused)
        return 1;
    connection->eof = true;
    connection->callback->readAvailable(0);
    if (eof != 1 || connection->callback)
        return 1;
    if (base.close([&] { ++closed; }) || closed != 1 || connection->callback ||
        connection->resets != 1 || connection->stops != 1)
        return 1;
    if (base.write(shared_buffer{}) != errc::shutdown)
        return 1;
    connection.reset();
    stream.reset();
    if (!lifetime.expired())
        return 1;
    auto next = std::make_shared<quic::raftpp_test_socket>();
    auto current = std::make_unique<mvfst_stream>(next, 0);
    bool inside = false, early_close = false;
    int deferred = 0;
    if (current->write(shared_buffer::copy(bytes), [&](error_code error) {
            if (error)
                return;
            inside = true;
            (void)current->close([&] {
                early_close = inside;
                ++deferred;
            });
            current.reset();
            inside = false;
        }))
        return 1;
    return early_close || deferred != 1;
}
