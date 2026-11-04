# TCP 전송

[English](../en/tcp_transport.md) · [libuv I/O](uv_io.md) · [노드 API](node.md) · [소유 stream](uv_stream.md) · [모듈별 API](modules/index.md)

`<raftpp/uv/tcp_transport.hpp>`의 `tcp_transport`는 libuv IPv4 TCP 소켓으로 메시지 수준 `transport` 인터페이스를 구현한다. `RAFTPP_WITH_UV=ON`일 때만 선언·빌드하며 LZ4·fixture 옵션과는 독립적이다. libuv 사용 조건을 전달하는 `raftpp::raftpp`를 링크한다.

## 노드에 연결하기

```cpp
#include <raftpp.hpp>
#include <uv.h>

raftpp::shared<raftpp::uv_io> make_backend(
    uv_loop_t& loop, raftpp::fs::path directory)
{
    raftpp::tcp_transport_options options;
    options.connect_retry = 100;
    auto network = std::make_shared<raftpp::tcp_transport>(
        loop, std::move(options));
    return std::make_shared<raftpp::uv_io>(
        loop, std::move(directory), std::move(network));
}
```

반환한 backend를 ID·광고 주소·FSM과 함께 `node`에 전달한다. 노드와 `uv_io`가 전송을 초기화·시작·종료한다. 디렉터리는 이미 존재해야 한다. 노드·backend 메서드와 모든 콜백은 같은 루프 스레드에서 실행하고 종료 완료까지 루프를 실행한다. 생성자는 표준 할당 예외를 던질 수 있다. `tcp_transport`는 복사·이동할 수 없으며 소멸자는 비동기 종료를 시작한다.

Linux에서는 루프 실행 전에 애플리케이션이 `SIGPIPE`를 처리하거나 무시하도록 설정한다. 예를 들어 프로세스 시작 시 `<csignal>`의 `std::signal(SIGPIPE, SIG_IGN)`을 호출한다. C libuv backend와 마찬가지로 끊어진 상대에게 쓰면 이 신호가 발생할 수 있다. 라이브러리는 프로세스 전체의 신호 처리기를 변경하지 않는다.

## 옵션과 주소

| `tcp_transport_options` 필드 | 기본값 | 의미 |
| --- | --- | --- |
| `bind_address` | 빈 문자열 | 광고 주소로 listen한다. 비어 있지 않은 재정의는 bind endpoint만 변경한다. |
| `connect_retry` | 1000 ms | 연결 시도 실패 사이의 대기 시간. 0도 허용한다. |
| `max_handshake_address` | 2048바이트 | 끝 NUL과 8바이트 정렬 패딩을 포함한 광고 주소의 제한. 로컬 초기화와 수신 handshake에 적용한다. |
| `limits` | `encoding_limits{}` | 송신 인코딩·수신 디코딩의 헤더·payload·엔트리 개수 제한. [인코딩](encoding.md)을 참고한다. |
| `tracing` | 기본 stderr tracer | 연결·메시지·재시도 진단. `set_tracer(tracer)`로 변경한다. [추적](tracing.md)을 참고한다. |

주소는 `host:service` 형식이다. service가 없거나 비어 있으면 `8080`을 사용한다. 호스트 이름을 IPv4로 해석하며 IPv6 리터럴 문법은 지원하지 않는다. listen에는 숫자 service가 필요하며 호스트를 동기 해석한다. bind 호스트가 비어 있으면 모든 IPv4 인터페이스에서 listen한다. 송신 주소 해석은 비동기이며 이름으로 된 service도 허용한다. 호스트와 service는 NUL을 포함해 플랫폼의 `NI_MAXHOST`·`NI_MAXSERV` 버퍼에 들어가야 한다. 문자열 내부 NUL은 `invalid`, 각 구성요소의 길이 초과는 `name_too_long`을 반환한다.

광고 주소는 상대 노드에서 접근할 수 있어야 한다. wildcard 주소나 포트 0에 bind해도 광고 주소를 다시 쓰지 않는다. 예를 들어 `node.example:8080`을 광고하고 `bind_address`를 `0.0.0.0:8080`으로 설정하면 모든 인터페이스에서 listen한다. bind 재정의는 handshake로 광고하는 식별자를 변경하지 않는다.

listen은 해석된 서로 다른 IPv4 endpoint에 각각 bind하며 같은 IP·포트가 반복되면 건너뛴다. 한 endpoint의 bind/listen이 실패하면 시작은 실패하고 이미 연 listener도 비동기로 닫는다. 오류 반환 자체가 정리 완료를 뜻하지 않으므로 `close` 완료까지 루프를 실행한 뒤 해당 포트·루프를 재사용한다. 송신 연결은 주소 목록 순서대로 시도하고 비동기 연결 실패 시 다음 주소로 진행한다.

## 설정 변경

```cpp
error_code set_bind_address(string_view address) noexcept;
void set_connect_retry_delay(unsigned msecs) noexcept;
```

`set_bind_address`는 `init` 전에, 전송 객체를 초기화하는 노드 생성 전을 포함해 호출한다. bind override의 소유 복사본을 교체하며 빈 값이면 advertised 주소로 복원한다. 위 주소 문법/길이 규칙을 따른다. 할당 실패는 `out_of_memory`, 과도한 길이는 `name_too_long`/`too_big`, 포함된 NUL은 `invalid`이며 이전 설정을 보존한다. 초기화 후에는 설정을 고정하므로 `busy`, 종료 중에는 `shutdown`이다. bind/listen 시 주소 해석 오류는 시작 오류로 처리한다. bind override를 바꿔도 advertised identity는 변경하지 않는다.

`set_connect_retry_delay`는 `transport`와 `uv_io`에서도 사용할 수 있다. 다음 실패의 타이머를 아직 예약하지 않은 기존 peer를 포함해 이후 연결 재시도의 예약에 적용한다. 이미 예약한 타이머의 만료 시각은 유지한다. 0도 허용하며 종료 중 변경은 무시한다. setter는 소유 이벤트 루프 스레드에서 실행하며 정상 연결된 소켓을 재연결하지 않는다.

## 직접 사용하는 전송 메서드

| 메서드 | 계약 |
| --- | --- |
| `init(id, address)` | 로컬 식별자를 복사하고 handshake를 준비한다. ID 0은 `bad_id`, 성공한 초기화의 반복은 `busy`다. 크기 제한 초과는 `too_big`이다. 검증·할당 실패는 미초기화 상태를 유지하므로 재시도할 수 있다. |
| `start(receive)` | bind/listen과 소유 메시지 수신 구독을 시작한다. 성공한 초기화와 비어 있지 않은 콜백이 필요하며, 아니면 `invalid`다. 성공한 시작의 반복은 `busy`다. 시작 실패는 종료를 시작하며 `close`로 정리 완료를 확인한다. |
| `send(shared<const message>, completion)` | 성공한 시작이 필요하다. 메시지의 ID·주소는 목적지다. null 메시지나 목적지 ID 0은 `bad_id`다. 수락 전에 인코딩하고 완료 콜백까지 메시지·인코딩 버퍼를 보관한다. |
| `close(callback = {})` | 수신을 중단하고 대기 송신을 취소하며 DNS·연결·쓰기 요청과 TCP·타이머 핸들을 정리한다. 요청 완료와 라이브러리가 보관한 메시지·수신 capture 해제 뒤 완료한다. 초기화 전이나 시작 실패 후에도 사용할 수 있다. |
| `last_error()` | 마지막으로 기록한 비동기 전송·상대 오류다. 일시적 연결 실패와 잘못된 수신도 설정할 수 있으며 치명적인 노드 상태나 송신 결과의 대체물이 아니다. |
| `error_details() const noexcept` | 마지막으로 관찰한 오류의 소유 진단 값. 연산·peer ID/주소·제공 가능한 네이티브 원인을 담고 할당하지 않는다. 반환/완료 코드와 구분하며 콜백 진입 시 복사한다. [오류 계약](errors.md)을 참고한다. |

`send`의 즉시 오류는 미수락을 의미하며 완료 콜백을 호출하지 않는다. 수락한 송신은 정확히 한 번 완료하며 재진입 호출 중에 완료할 수도 있다. 완료 전까지 메시지·payload를 수정하지 않는다. 성공은 소켓 쓰기의 완료를 의미하며 상대 수신·영속화·Raft commit을 증명하지 않는다.

수신 메시지에는 로컬 목적지 대신 연결 handshake의 송신자 ID·주소가 들어간다. 콜백 이후 보관한 엔트리·스냅샷 payload는 연결·전송 종료 후에도 유효하다. 송신자 식별자는 상대가 제공하며 프로토콜은 인증·암호화를 제공하지 않는다.

## 연결·실패·종료

RPC 연결은 단방향이다. 각 송신 연결은 cowsql/raft protocol-1 handshake를 보낸다. little-endian 64비트 protocol version·송신자 ID·패딩된 주소 길이와 NUL로 끝나고 0으로 패딩한 주소다. RPC framing은 기존 [메시지 형식](encoding.md)을 사용한다. 수신 handshake는 양수 ID·protocol 1·제한 이내의 0이 아닌 8의 배수 길이·NUL과 0 패딩이 있는 주소를 요구한다. 잘못되거나 잘린 스트림은 독립적으로 닫으며 다른 상대·listener는 계속 동작한다.

송신 client는 목적지 ID로 구분한다. 주소가 바뀌면 기존 client를 종료하고 그 대기 송신을 취소한다. 해석한 IPv4 결과를 순서대로 시도하며 실패한 소켓을 닫은 뒤 다음 주소를 시도한다. 연결·handshake 실패는 재시도한다. 비동기 연결 시도 실패 뒤 최신 대기 메시지 3개를 남기고 이전 메시지는 `no_connection`으로 완료한다. 주소 해석 제출의 즉시 실패는 퇴출 없이 재시도를 예약한다. 이는 연결 실패 시의 큐 정책이며 정상 연결에서 동시에 쓸 수 있는 요청 수의 고정 제한이 아니다.

준비된 연결에서 RPC 쓰기 제출이 즉시 실패하면 콜백 없이 `io_error`를 반환한다. 이미 대기하던 RPC가 연결 후 제출에 실패하면 `io_error`로 완료하고 남은 큐를 계속 처리한다. 비동기 RPC 쓰기 실패는 `io_error`로 완료하고 기존 소켓을 닫은 뒤 재연결한다. 네이티브 종료로 취소된 연산은 `canceled`로 완료하며 이미 완료한 쓰기는 성공할 수 있다. 연결을 기다리던 수락된 송신은 종료 시 취소한다.

수신·송신 콜백에서 close를 호출하거나 facade의 마지막 소유자를 파괴해도 된다. 실행 중인 콜백과 내부 native 상태는 반환·정리 완료까지 유지된다. 종료 중 새 송신은 `shutdown`을 반환한다. 동시에 하나의 종료 콜백만 등록한다. 콜백 없는 close 반복은 허용하며 종료 완료 후 전달한 콜백은 인라인 호출한다. 수신·송신 완료 콜백의 예외는 종료를 시작하고 `invalid`를 설정한다. 예외는 native 콜백 경계를 넘지 않으며 종료 콜백의 예외는 `invalid`로 기록한다.
