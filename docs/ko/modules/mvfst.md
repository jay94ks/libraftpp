# mvfst 모듈 API

[English](../../en/modules/mvfst.md) · [모듈 목록](index.md)

`mvfst_stream final : byte_stream`은 연결된 QUIC 양방향 스트림을 core의 바이트 스트림으로 사용한다. `RAFTPP_MODULE_MVFST=ON`, `raftpp::mvfst`, `<raftpp/mvfst/mvfst.hpp>`를 사용한다. native 의존 타깃은 `mvfst::mvfst_transport`이며 SDK/Folly/Fizz 준비는 [의존성](../dependencies.md)을 따른다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS mvfst)
target_link_libraries(app PRIVATE raftpp::mvfst)
```

## 생성과 소유권

```cpp
mvfst_stream(shared<quic::QuicSocketLite> connection, quic::StreamId stream);
```

인자는 이미 연결된 connection의 shared owner와 유효한 양방향 stream ID다. null connection 또는 단방향 ID는 `std::invalid_argument("mvfst requires a bidirectional stream and connection")`를 던진다. 내부 상태 할당은 예외를 던질 수 있다. 생성자가 ID의 실제 존재/연결 수립/peer 인증을 대신 검증하지 않는다.

adapter가 해당 스트림의 읽기 콜백과 read/write/reset/stop 수명을 독점한다. 다른 native 콜백을 등록하거나 같은 스트림을 외부에서 reset하지 않는다. 다른 스트림은 같은 connection을 공유할 수 있다. connection/event base는 스트림 사용과 native 정리가 끝날 때까지 유지해야 한다. 모든 호출·파괴는 connection의 event-base 스레드에서 수행한다. 객체는 복사/이동할 수 없다.

## 공개 메서드

아래 메서드는 모두 `noexcept`다. 연산은 성공 시 빈 `error_code`를 반환하며 `error_details()`는 `error_info`를 반환한다.

| 메서드 | 입력·반환·콜백 계약 |
| --- | --- |
| `read_start(stream_read_callback callback, size_t buffer_size = 65536)` | 콜백과 양수 read 크기가 필요하다. 첫 구독은 `setReadCallback`, 재개는 `resumeRead`를 사용한다. 빈 콜백/크기 0은 `invalid`, 기존 구독은 `busy`, 닫힌 스트림은 `shutdown`. |
| `read_stop()` | 구독을 제거하고 native 읽기를 pause한다. 아직 등록되지 않았거나 닫힌 경우 성공. |
| `write(view<const shared_buffer> buffers, completion callback = {})` | 순서대로 하나의 SDK 소유 `IOBuf`에 복사하고 `writeChain`에 넘긴다. 성공 콜백은 제출 함수 안에서 즉시 실행할 수 있다. |
| `write(shared_buffer bytes, completion callback = {})` | 상속한 단일 버퍼 편의 오버로드. |
| `close(close_callback callback = {})` | reader 해제, 해당 스트림 reset/stopSending, connection owner 해제 뒤 완료한다. 대기 중인 close observer가 있으면 새 nonempty observer는 `busy`. |
| `error_details() const` | 최근 기록한 오류의 소유 진단을 반환한다. native local error는 `cause`의 `mvfst` 카테고리에 담으며 native read error는 메시지를 담는다. |

읽기는 native `IOBuf`를 소유한 `shared_buffer`로 복사한다. 성공 fragment는 콜백 반환/스트림 종료 뒤에도 보관할 수 있다. EOF는 `no_connection`과 빈 버퍼로 전달하며 그 구독을 제거한다. native 읽기 실패도 구독을 제거한다. fragment 크기는 read 요청과 native 데이터에 따르며 RPC 경계가 아니다.

즉시 쓰기 실패에는 완료 콜백이 없다. 수락된 쓰기의 성공은 SDK가 복사본의 소유권을 받았음을 뜻하며 실제 전송 완료·peer ACK·commit을 뜻하지 않는다. 호출자는 성공 반환 뒤 원본 owner를 해제할 수 있다. SDK의 flow control/native 오류는 `io_error`와 native 원인으로 보고한다. 합산 크기 overflow/길이 오류는 `too_big`, 할당 실패는 `out_of_memory`, 기타 예외는 `io_error`다. 닫힌 스트림의 쓰기는 `shutdown`이다.

close는 native reset/stopSending의 원격 응답을 기다리지 않는 로컬 정리다. 그 native 호출의 오류는 close 반환값에 전파하지 않는다. 활성 사용자 콜백 중 close하면 그 콜백이 반환한 뒤 close observer를 실행한다. 그 밖에는 즉시 실행할 수 있다. 소멸자는 close를 시작한다. 읽기·쓰기·close 콜백은 예외를 던지지 않아야 한다.

## 사용 예제

```cpp
#include <raftpp/mvfst/mvfst.hpp>
#include <raftpp/framed_transport.hpp>

raftpp::shared<raftpp::framed_transport> make_mvfst_transport(
    raftpp::shared<quic::QuicSocketLite> connection, quic::StreamId stream_id)
{
    auto stream = std::make_shared<raftpp::mvfst_stream>(std::move(connection), stream_id);
    auto network = std::make_shared<raftpp::framed_transport>();
    if (auto error = network->attach(2, "peer-2", std::move(stream)))
        throw raftpp::system_error(error);
    return network;
}
```

애플리케이션 connector가 QUIC 연결·TLS·peer 인증·stream ID를 준비한 뒤 event-base 스레드에서 호출한다. I/O/노드도 같은 직렬 실행 영역을 사용해야 하며 UV loop와 다른 event base를 자동 연결하지 않는다. 반환한 transport의 init/start/close는 I/O 백엔드에 맡긴다. listener·재접속·영속 I/O는 이 스트림 API의 역할에 포함되지 않는다. [공통 계약](../streams.md)을 함께 따른다.
