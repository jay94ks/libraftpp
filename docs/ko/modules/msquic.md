# msquic 모듈 API

[English](../../en/modules/msquic.md) · [모듈 목록](index.md)

`RAFTPP_MODULE_MSQUIC=ON`에서 `raftpp::msquic`와 `<raftpp/msquic/msquic.hpp>`를 제공한다. core와 native `msquic::msquic` 타깃을 연결한다. SDK 준비·TLS 빌드 선택은 [의존성](../dependencies.md)을 따른다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS msquic)
target_link_libraries(app PRIVATE raftpp::msquic)
```

## API 테이블 소유권

```cpp
shared<const QUIC_API_TABLE> open_msquic_api();
```

`MsQuicOpen2`로 API 테이블을 열고 마지막 shared owner가 사라질 때 `MsQuicClose`를 호출한다. 실패는 `std::system_error`이며 그 코드의 카테고리는 `msquic`이다. shared owner 생성은 할당 예외를 던질 수 있다. 반환한 테이블을 외부에서 직접 `MsQuicClose`하지 않는다.

## 연결된 스트림 인계

```cpp
msquic_stream(shared<const QUIC_API_TABLE> api, HQUIC stream,
              shared<void> connection_owner);
```

모든 인자는 유효해야 하며 `stream`은 이미 시작된 연결의 양방향 스트림이어야 한다. null 인자는 `std::invalid_argument("msquic requires API, stream and connection ownership")`를 던진다. constructor는 stream 방향/연결 완료/인증을 대신 검증하지 않는다. `connection_owner`는 native connection과 registration, 필요한 configuration의 수명을 유지하는 애플리케이션 owner다. 단순한 대여 포인터를 shared로 감싼 값은 이 요구를 만족하지 않는다.

생성자는 수신을 비활성화한 뒤 adapter의 native callback을 등록한다. 수신 비활성화 실패는 `std::system_error`를 던진다. 실패 시 native stream은 호출자가 계속 소유하며 native receive 설정이 변경되었을 가능성을 고려해 정리한다. 성공한 뒤 native stream handle/callback은 adapter가 독점 소유한다. 외부에서 `StreamClose`하거나 callback을 교체하지 않는다. API table과 connection owner는 native 정리까지 내부에서 유지한다. 객체는 복사/이동할 수 없다.

모든 메서드·파괴는 connection의 직렬 MsQuic callback 실행 영역에서 수행한다. 다른 connection의 worker가 같은 스레드라는 보장은 없다. 어댑터는 callback을 임의의 애플리케이션 loop로 옮기지 않는다.

## 공개 메서드

아래 메서드는 모두 `noexcept`다. 연산은 성공 시 빈 `error_code`를 반환하며 `error_details()`는 `error_info`를 반환한다.

| 메서드 | 입력·반환·콜백 계약 |
| --- | --- |
| `read_start(stream_read_callback callback, size_t buffer_size = 65536)` | 수신 활성화. 빈 콜백/크기 0은 `invalid`, 기존 구독은 `busy`, 종료 중/후에는 `shutdown`. 크기는 양수 검사에만 쓰며 native RECEIVE 이벤트 크기나 할당 상한을 제한하지 않는다. |
| `read_stop()` | 구독 제거와 native 수신 비활성화. 이미 닫혔다면 성공. |
| `write(view<const shared_buffer> buffers, completion callback = {})` | owner/QUIC_BUFFER 배열을 SEND_COMPLETE까지 유지한다. 배열 개수와 각 길이는 `uint32_t` 범위여야 하며 초과는 `too_big`. 빈 쓰기도 허용한다. 종료 중/후는 `shutdown`. |
| `write(shared_buffer bytes, completion callback = {})` | 상속한 단일 버퍼 편의 오버로드. |
| `close(close_callback callback = {})` | 읽기를 멈추고 native abort shutdown을 제출한다. 대기 중인 close observer가 있으면 새 nonempty observer는 `busy`. |
| `error_details() const` | 최근 기록한 소유 진단. native status는 `cause`의 `msquic` 카테고리에 보존한다. |

native OOM은 `out_of_memory`, 다른 native 실패는 `io_error`다. 쓰기 요청/버퍼 배열 할당 실패는 `out_of_memory`, 길이 오류는 `too_big`다. 즉시 쓰기 실패는 콜백을 호출하지 않는다. 수락한 쓰기는 SEND_COMPLETE에서 한 번 완료하며 native Canceled는 `errc::canceled`다. 입력 배열은 제출 뒤 해제할 수 있지만 공유 데이터는 완료 전 변경하지 않는다. 성공은 로컬 SDK 완료이며 원격 애플리케이션 ACK/commit은 아니다.

RECEIVE 데이터는 native 콜백이 반환하기 전에 소유한 `shared_buffer`로 복사한다. 성공 버퍼는 콜백 밖에서도 유지할 수 있다. peer send shutdown/abort 또는 연결 종료는 읽기 구독에 `no_connection`과 빈 버퍼를 전달한다. 수신 할당 실패는 `out_of_memory`로 알린다. fragment 경계는 write/RPC 경계가 아니다.

close는 모든 SEND_COMPLETE와 SHUTDOWN_COMPLETE, 활성 사용자 콜백과 중첩 native 이벤트 처리가 끝난 뒤 `StreamClose`하고 connection owner를 해제하며 close observer를 실행한다. 이미 닫혔다면 콜백은 즉시 실행한다. 최초 shutdown 제출이 OOM이면 같은 native worker에서 allocation 없는 inline abort를 시도한다. 최종 제출 실패 시 close observer를 해제하고 오류를 반환하므로 호출자는 실행 영역을 유지하며 종료를 다시 요청할 수 있다. 소멸자는 close를 시도하며 동기 대기하지 않는다. 오류를 처리해야 하는 종료는 명시적 close와 완료 콜백을 사용한다. 모든 사용자 콜백은 예외를 던지지 않아야 한다.

## 사용 예제

```cpp
#include <raftpp/msquic/msquic.hpp>

raftpp::shared<raftpp::byte_stream> adopt_msquic_stream(
    raftpp::shared<const QUIC_API_TABLE> api, HQUIC connected_stream,
    raftpp::shared<void> connection_owner)
{
    return std::make_shared<raftpp::msquic_stream>(
        std::move(api), connected_stream, std::move(connection_owner));
}
```

애플리케이션 connector에서 `auto api = raftpp::open_msquic_api();`로 얻은 같은 테이블을 native 연결과 스트림 생성에 사용한다. 인증된 연결/registration owner와 시작된 양방향 stream을 준비한 뒤 그 connection callback 실행 영역에서 위 함수를 호출한다. 호출자는 생성 성공 여부를 확인하고 실패 정리를 끝낼 때까지 API/connection owner 사본을 유지한다. 생성 실패에는 호출자가 native stream을 정리하고, 성공 후에는 adapter에 종료를 맡긴다. [core의 `framed_transport`](core.md)에 연결할 때 I/O/노드와 같은 직렬 실행 영역을 사용한다. listener·TLS 설정·재접속·별도 영속 I/O는 connector/백엔드가 담당한다.
