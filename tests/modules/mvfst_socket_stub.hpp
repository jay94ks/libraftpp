#ifndef RAFTPP_TEST_MVFST_SOCKET_STUB_HPP
#define RAFTPP_TEST_MVFST_SOCKET_STUB_HPP
// API signatures originate in Meta mvfst (MIT): deps/mvfst/LICENSE.
// The SDK interface is intentionally broad. Unused pure virtual methods abort;
// the test exercises only the seven socket operations used by mvfst_stream.
#include <quic/api/QuicSocketLite.h>
#include <cstdlib>
namespace quic {
    class raftpp_test_socket final : public QuicSocketLite {
    public:
        ReadCallback* callback = nullptr;
        BufPtr incoming;
        BufPtr written;
        bool eof = false, paused = false, fail_write = false;
        int resets = 0, stops = 0;
        quic::Expected<std::pair<BufPtr, bool>, LocalErrorCode> read(StreamId, size_t) override {
            return std::make_pair(std::move(incoming), eof);
        }
        quic::Expected<void, LocalErrorCode> setReadCallback(
            StreamId, ReadCallback* value,
            Optional<ApplicationErrorCode> = GenericApplicationErrorCode::NO_ERROR) override {
            callback = value;
            return {};
        }
        quic::Expected<void, LocalErrorCode> pauseRead(StreamId) override {
            paused = true;
            return {};
        }
        quic::Expected<void, LocalErrorCode> resumeRead(StreamId) override {
            paused = false;
            return {};
        }
        WriteResult writeChain(StreamId, BufPtr bytes, bool,
                               ByteEventCallback* = nullptr) override {
            if (fail_write)
                return quic::make_unexpected(LocalErrorCode::INVALID_OPERATION);
            written = std::move(bytes);
            return {};
        }
        quic::Expected<void, LocalErrorCode> resetStream(StreamId, ApplicationErrorCode) override {
            ++resets;
            return {};
        }
        quic::Expected<void, LocalErrorCode> stopSending(StreamId, ApplicationErrorCode) override {
            ++stops;
            return {};
        }
        quic::Expected<StreamId, LocalErrorCode>
        createBidirectionalStream([[maybe_unused]] bool replaySafe) override {
            std::abort();
        }
        quic::Expected<StreamId, LocalErrorCode>
        createUnidirectionalStream([[maybe_unused]] bool replaySafe) override {
            std::abort();
        }
        uint64_t getNumOpenableBidirectionalStreams() const override {
            std::abort();
        }
        uint64_t getNumOpenableUnidirectionalStreams() const override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        setStopSendingCallback([[maybe_unused]] StreamId id,
                               [[maybe_unused]] StopSendingCallback* ss) noexcept override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        registerByteEventCallback([[maybe_unused]] const ByteEvent::Type type,
                                  [[maybe_unused]] const StreamId id,
                                  [[maybe_unused]] const uint64_t offset,
                                  [[maybe_unused]] ByteEventCallback* cb) override {
            std::abort();
        }
        size_t
        getNumByteEventCallbacksForStream([[maybe_unused]] const StreamId streamId) const override {
            std::abort();
        }
        size_t
        getNumByteEventCallbacksForStream([[maybe_unused]] const ByteEvent::Type type,
                                          [[maybe_unused]] const StreamId streamId) const override {
            std::abort();
        }
        void cancelAllByteEventCallbacks() override {
            std::abort();
        }
        void cancelByteEventCallbacks([[maybe_unused]] const ByteEvent::Type type) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        setDatagramCallback([[maybe_unused]] DatagramCallback* cb) override {
            std::abort();
        }
        uint16_t getDatagramSizeLimit() const override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode> writeDatagram([[maybe_unused]] BufPtr buf) override {
            std::abort();
        }
        quic::Expected<std::vector<ReadDatagram>, LocalErrorCode>
        readDatagrams([[maybe_unused]] size_t atMost) override {
            std::abort();
        }
        quic::Expected<std::vector<BufPtr>, LocalErrorCode>
        readDatagramBufs([[maybe_unused]] size_t atMost) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        registerDeliveryCallback([[maybe_unused]] StreamId id, [[maybe_unused]] uint64_t offset,
                                 [[maybe_unused]] ByteEventCallback* cb) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        notifyPendingWriteOnConnection([[maybe_unused]] ConnectionWriteCallback* wcb) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        notifyPendingWriteOnStream([[maybe_unused]] StreamId id,
                                   [[maybe_unused]] StreamWriteCallback* wcb) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        unregisterStreamWriteCallback([[maybe_unused]] StreamId) override {
            std::abort();
        }
        bool good() const override {
            std::abort();
        }
        bool error() const override {
            std::abort();
        }
        void close([[maybe_unused]] Optional<QuicError> errorCode) override {
            std::abort();
        }
        void closeNow([[maybe_unused]] Optional<QuicError> errorCode) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        stopSendingAndDropIngress([[maybe_unused]] StreamId id,
                                  [[maybe_unused]] ApplicationErrorCode error) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode> stopSendingAndDropIngressForAllStreams(
            [[maybe_unused]] ApplicationErrorCode error) override {
            std::abort();
        }
        void setConnectionSetupCallback(
            [[maybe_unused]] folly::MaybeManagedPtr<ConnectionSetupCallback> callback) override {
            std::abort();
        }
        void
        setConnectionCallback([[maybe_unused]] folly::MaybeManagedPtr<ConnectionCallback> callback,
                              [[maybe_unused]] ConnectionCallbackOptions options) override {
            std::abort();
        }
        Optional<LocalErrorCode> setControlStream([[maybe_unused]] StreamId id) override {
            std::abort();
        }
        void cancelDeliveryCallbacksForStream([[maybe_unused]] StreamId streamId) override {
            std::abort();
        }
        void cancelDeliveryCallbacksForStream([[maybe_unused]] StreamId streamId,
                                              [[maybe_unused]] uint64_t offset) override {
            std::abort();
        }
        void cancelByteEventCallbacksForStream(
            [[maybe_unused]] const StreamId id,
            [[maybe_unused]] const Optional<uint64_t>& offset) override {
            std::abort();
        }
        void cancelByteEventCallbacksForStream(
            [[maybe_unused]] const ByteEvent::Type type, [[maybe_unused]] const StreamId id,
            [[maybe_unused]] const Optional<uint64_t>& offset) override {
            std::abort();
        }
        void setReceiveWindow([[maybe_unused]] StreamId id,
                              [[maybe_unused]] size_t recvWindowSize) override {
            std::abort();
        }
        void setSendBuffer([[maybe_unused]] StreamId id, [[maybe_unused]] size_t maxUnacked,
                           [[maybe_unused]] size_t maxUnsent) override {
            std::abort();
        }
        void setTransportSettings([[maybe_unused]] TransportSettings transportSettings) override {
            std::abort();
        }
        void setCongestionControl([[maybe_unused]] CongestionControlType type) override {
            std::abort();
        }
        void addPacketProcessor(
            [[maybe_unused]] std::shared_ptr<PacketProcessor> packetProcessor) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode> setKnob([[maybe_unused]] uint64_t knobSpace,
                                                     [[maybe_unused]] uint64_t knobId,
                                                     [[maybe_unused]] BufPtr knobBlob) override {
            std::abort();
        }
        bool isKnobSupported() const override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        setPriorityQueue([[maybe_unused]] std::unique_ptr<PriorityQueue> queue) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        setStreamPriority([[maybe_unused]] StreamId id,
                          [[maybe_unused]] PriorityQueue::Priority pri) override {
            std::abort();
        }
        quic::Expected<void, LocalErrorCode>
        setMaxPacingRate([[maybe_unused]] uint64_t rateBytesPerSec) override {
            std::abort();
        }
        void setThrottlingSignalProvider(
            [[maybe_unused]] std::shared_ptr<ThrottlingSignalProvider>) override {
            std::abort();
        }
        std::shared_ptr<QuicEventBase> getEventBase() const override {
            std::abort();
        }
        const QuicConnectionStateBase* getState() const override {
            std::abort();
        }
        quic::Expected<StreamTransportInfo, LocalErrorCode>
        getStreamTransportInfo([[maybe_unused]] StreamId id) const override {
            std::abort();
        }
        const quic::SocketAddress& getPeerAddress() const override {
            std::abort();
        }
        const quic::SocketAddress& getOriginalPeerAddress() const override {
            std::abort();
        }
        const quic::SocketAddress& getLocalAddress() const override {
            std::abort();
        }
        Optional<std::vector<uint8_t>>
        getExportedKeyingMaterial([[maybe_unused]] const std::string& label,
                                  [[maybe_unused]] const Optional<ByteRange>& context,
                                  [[maybe_unused]] uint16_t keyLength) const override {
            std::abort();
        }
        Optional<std::string> getAppProtocol() const override {
            std::abort();
        }
        uint64_t getConnectionBufferAvailable() const override {
            std::abort();
        }
        Optional<std::vector<TransportParameter>> getPeerTransportParams() const override {
            std::abort();
        }
        quic::Expected<FlowControlState, LocalErrorCode>
        getStreamFlowControl([[maybe_unused]] StreamId id) const override {
            std::abort();
        }
        const TransportSettings& getTransportSettings() const override {
            std::abort();
        }
        TransportInfo getTransportInfo() const override {
            std::abort();
        }
        uint64_t maxWritableOnConn() const override {
            std::abort();
        }
        QuicNodeType getNodeType() const noexcept override {
            std::abort();
        }
        QuicConnectionStats getConnectionsStats() const override {
            std::abort();
        }
    };
}
#endif
