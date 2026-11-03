#include <raftpp/msquic/msquic.hpp>
#include <cstring>

namespace {
    struct native_stream {
        QUIC_STREAM_CALLBACK_HANDLER handler = nullptr;
        void* context = nullptr;
        void* request = nullptr;
        const QUIC_BUFFER* buffers = nullptr;
        uint32_t count = 0;
        bool enabled = false, closing = false, closed = false, fail_send = false;
        bool shutdown_oom = false, shutdown_inline = false;
        int shutdown_calls = 0;
        HQUIC handle() {
            return reinterpret_cast<HQUIC>(this);
        }
        QUIC_STATUS emit(QUIC_STREAM_EVENT& event) {
            return handler(handle(), context, &event);
        }
    };
    native_stream& get(HQUIC handle) {
        return *reinterpret_cast<native_stream*>(handle);
    }
    void QUIC_API set_callback(HQUIC handle, void* handler, void* context) {
        get(handle).handler = reinterpret_cast<QUIC_STREAM_CALLBACK_HANDLER>(handler);
        get(handle).context = context;
    }
    QUIC_STATUS QUIC_API set_enabled(HQUIC handle, BOOLEAN enabled) {
        get(handle).enabled = enabled;
        return QUIC_STATUS_SUCCESS;
    }
    QUIC_STATUS QUIC_API send(HQUIC handle, const QUIC_BUFFER* buffers, uint32_t count,
                              QUIC_SEND_FLAGS, void* request) {
        auto& stream = get(handle);
        if (stream.fail_send)
            return QUIC_STATUS_INVALID_STATE;
        stream.request = request;
        stream.buffers = buffers;
        stream.count = count;
        return QUIC_STATUS_SUCCESS;
    }
    QUIC_STATUS QUIC_API shutdown(HQUIC handle, QUIC_STREAM_SHUTDOWN_FLAGS flags, QUIC_UINT62) {
        auto& stream = get(handle);
        ++stream.shutdown_calls;
        if (stream.shutdown_oom && !(flags & QUIC_STREAM_SHUTDOWN_FLAG_INLINE))
            return QUIC_STATUS_OUT_OF_MEMORY;
        stream.closing = true;
        if (stream.shutdown_inline) {
            QUIC_STREAM_EVENT stopped{};
            stopped.Type = QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE;
            return stream.emit(stopped);
        }
        return QUIC_STATUS_SUCCESS;
    }
    void QUIC_API close(HQUIC handle) {
        get(handle).closed = true;
    }
}
int main() {
    using namespace raftpp;
    // The real SDK opens/closes its API. A controlled native callback table
    // covers completion ordering without testing upstream TLS/UDP machinery.
    auto actual_api = open_msquic_api();
    if (!actual_api)
        return 1;
    actual_api.reset();
    auto api = std::make_shared<QUIC_API_TABLE>();
    api->SetCallbackHandler = set_callback;
    api->StreamReceiveSetEnabled = set_enabled;
    api->StreamSend = send;
    api->StreamShutdown = shutdown;
    api->StreamClose = close;
    auto native = std::make_shared<native_stream>();
    auto stream = std::make_unique<msquic_stream>(api, native->handle(), native);
    int received = 0, sent = 0, drained = 0;
    const array<byte, 3> bytes{byte{1}, byte{2}, byte{3}};
    if (stream->read_start([&](error_code error, shared_buffer value) {
            if (!error && value.size() == 3 &&
                std::memcmp(value.bytes().data(), bytes.data(), 3) == 0)
                ++received;
        }) ||
        !native->enabled)
        return 1;
    QUIC_BUFFER incoming{3, reinterpret_cast<uint8_t*>(const_cast<byte*>(bytes.data()))};
    QUIC_STREAM_EVENT receive{};
    receive.Type = QUIC_STREAM_EVENT_RECEIVE;
    receive.RECEIVE.Buffers = &incoming;
    receive.RECEIVE.BufferCount = 1;
    receive.RECEIVE.TotalBufferLength = 3;
    if (QUIC_FAILED(native->emit(receive)) || received != 1)
        return 1;
    if (stream->read_stop() || native->enabled)
        return 1;
    receive.RECEIVE.TotalBufferLength = 3;
    if (QUIC_FAILED(native->emit(receive)) || receive.RECEIVE.TotalBufferLength || received != 1)
        return 1;
    auto payload = shared_buffer::copy(bytes);
    std::weak_ptr<const byte> lifetime = payload.storage();
    if (stream->write(payload,
                      [&](error_code error) {
                          if (!error)
                              ++sent;
                      }) ||
        sent)
        return 1;
    payload = {};
    if (lifetime.expired() || !native->buffers || native->buffers[0].Length != 3 ||
        std::memcmp(native->buffers[0].Buffer, bytes.data(), 3) != 0)
        return 1;
    if (stream->close([&] { ++drained; }) || drained || !native->closing)
        return 1;
    stream.reset();
    QUIC_STREAM_EVENT complete{};
    complete.Type = QUIC_STREAM_EVENT_SEND_COMPLETE;
    complete.SEND_COMPLETE.ClientContext = native->request;
    if (QUIC_FAILED(native->emit(complete)) || sent != 1 || !lifetime.expired() || drained)
        return 1;
    QUIC_STREAM_EVENT stopped{};
    stopped.Type = QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE;
    if (QUIC_FAILED(native->emit(stopped)) || drained != 1 || !native->closed)
        return 1;
    auto second = std::make_shared<native_stream>();
    second->fail_send = true;
    second->shutdown_oom = true;
    msquic_stream rejected{api, second->handle(), second};
    if (!rejected.write(shared_buffer{}, [&](error_code) { ++sent; }) || sent != 1)
        return 1;
    if (rejected.close() || second->shutdown_calls != 2)
        return 1;
    if (QUIC_FAILED(second->emit(stopped)) || !second->closed)
        return 1;
    auto third = std::make_shared<native_stream>();
    third->shutdown_inline = true;
    auto current = std::make_unique<msquic_stream>(api, third->handle(), third);
    bool inside = false, early_close = false;
    int deferred = 0;
    if (current->read_start([&](error_code, shared_buffer) {
            inside = true;
            (void)current->close([&] {
                early_close = inside;
                ++deferred;
            });
            current.reset();
            inside = false;
        }))
        return 1;
    receive.RECEIVE.TotalBufferLength = 3;
    if (QUIC_FAILED(third->emit(receive)) || early_close || deferred != 1 || !third->closed)
        return 1;
    return 0;
}
