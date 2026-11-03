#include <raftpp/asio/asio.hpp>
#include <raftpp/framed_transport.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <array>
#include <iostream>

namespace {
    int fail(int line) {
        std::cerr << "asio contract failed at line " << line << "\n";
        return 1;
    }
}
int main() {
    using namespace raftpp;
    asio::io_context context;
    asio_executor schedule{context.get_executor()};
    int calls = 0;
    if (schedule.post([&] { ++calls; }) || calls)
        return fail(__LINE__);
    context.run();
    if (calls != 1)
        return fail(__LINE__);
    context.restart();
    asio::ip::tcp::acceptor listener{context, {asio::ip::address_v4::loopback(), 0}};
    asio::ip::tcp::socket client{context};
    client.connect(listener.local_endpoint());
    auto a = std::make_shared<asio_stream>(std::move(client));
    auto b = std::make_shared<asio_stream>(listener.accept());
    listener.close();
    framed_transport sender, receiver;
    if (sender.attach(2, "peer-b", a) || receiver.attach(1, "peer-a", b) ||
        sender.init(1, "peer-a") || receiver.init(2, "peer-b"))
        return fail(__LINE__);
    int received = 0, sent = 0, closed = 0;
    error_code failure;
    if (sender.start([](message) {}) || receiver.start([&](message value) {
            auto* vote = std::get_if<request_vote>(&value.payload);
            if (!vote || vote->term != 19 || value.server_id != 1 ||
                value.server_address != "peer-a")
                failure = errc::malformed;
            ++received;
            receiver.close([&] { ++closed; });
            sender.close([&] { ++closed; });
        }))
        return fail(__LINE__);
    auto value = std::make_shared<message>();
    value->server_id = 2;
    value->server_address = "peer-b";
    request_vote vote;
    vote.term = 19;
    vote.candidate_id = 1;
    value->payload = vote;
    std::weak_ptr<message> lifetime = value;
    if (sender.send(value, [&](error_code error) {
            if (error)
                failure = error;
            ++sent;
        }))
        return fail(__LINE__);
    value.reset();
    context.run();
    if (failure || received != 1 || sent != 1 || closed != 2 || !lifetime.expired())
        return fail(__LINE__);
    if (a->write(shared_buffer{}) != errc::shutdown)
        return fail(__LINE__);
    // Close with an outstanding native read and owner destruction.
    context.restart();
    asio::ip::tcp::acceptor pending_listener{context, {asio::ip::address_v4::loopback(), 0}};
    asio::ip::tcp::socket pending_client{context};
    pending_client.connect(pending_listener.local_endpoint());
    auto pending_peer = pending_listener.accept();
    pending_listener.close();
    // Queue a successful native read, pause before dispatch, then resume.
    const char byte = 'x';
    pending_peer.write_some(asio::buffer(&byte, 1));
    auto pending = std::make_unique<asio_stream>(std::move(pending_client));
    int resumed = 0;
    if (pending->read_start([&](error_code, shared_buffer) { ++resumed; }, 1) ||
        pending->read_stop())
        return fail(__LINE__);
    context.run();
    if (resumed)
        return fail(__LINE__);
    context.restart();
    if (pending->read_start(
            [&](error_code error, shared_buffer value) {
                if (!error && value.size() == 1 &&
                    value.bytes()[0] == static_cast<raftpp::byte>('x'))
                    ++resumed;
            },
            1) ||
        resumed != 1 || pending->read_stop())
        return fail(__LINE__);

    int unexpected_read = 0, drained = 0;
    if (pending->read_start([&](error_code, shared_buffer) { ++unexpected_read; }, 1) ||
        pending->close([&] { ++drained; }))
        return fail(__LINE__);
    pending.reset();
    context.run();
    if (unexpected_read || drained != 1)
        return fail(__LINE__);
    context.restart();
    asio::ip::tcp::acceptor final_listener{context, {asio::ip::address_v4::loopback(), 0}};
    asio::ip::tcp::socket final_client{context};
    final_client.connect(final_listener.local_endpoint());
    auto final_peer = final_listener.accept();
    final_listener.close();
    auto final_stream = std::make_unique<asio_stream>(std::move(final_client));
    bool inside = false, early_close = false;
    int final_closed = 0;
    if (final_stream->write(shared_buffer::copy(std::as_bytes(std::span{&byte, 1})),
                            [&](error_code error) {
                                if (error)
                                    failure = error;
                                inside = true;
                                if (final_stream->close([&] {
                                        early_close = inside;
                                        ++final_closed;
                                    }))
                                    failure = errc::io_error;
                                final_stream.reset();
                                inside = false;
                            }))
        return fail(__LINE__);
    context.run();
    if (failure || early_close || final_closed != 1) {
        std::cerr << "write/close result: " << failure.message() << ", early=" << early_close
                  << ", closed=" << final_closed << "\n";
        return fail(__LINE__);
    }
    return 0;
}
