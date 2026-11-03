# Stream 플러그인의 메시지 전송

[English](../en/stream_transport.md) · [소유 stream](uv_stream.md) · [libuv I/O](uv_io.md) · [인코딩](encoding.md)

`<raftpp/uv/stream_transport.hpp>`의 `stream_transport`는 호출자가 선택한 `shared<uv_stream_transport>` 위에서 `transport`를 구현한다. 완전한 Raft 메시지의 framing, 대기 쓰기 소유권과 raw connector 재연결을 담당한다. `RAFTPP_WITH_UV=ON`이 필요하며 LZ4·fixture와 독립적이다. `raftpp::raftpp`만 링크한다. 호출·콜백은 지정한 libuv 루프에서 순차 실행하며 종료 완료까지 루프를 유지한다.

## 생성과 연결

```cpp
#include <raftpp/uv/stream_transport.hpp>
#include <raftpp/uv/uv_io.hpp>

raftpp::shared<raftpp::uv_io> make_stream_backend(
    uv_loop_s& loop,
    raftpp::fs::path directory,
    raftpp::shared<raftpp::uv_stream_transport> connector)
{
    auto messages = std::make_shared<raftpp::stream_transport>(loop, std::move(connector));
    return std::make_shared<raftpp::uv_io>(loop, std::move(directory), std::move(messages));
}
```

connector로 `tcp_stream_transport` 또는 `uv_stream`으로 초기화된 TCP/pipe/TTY stream을 전달하는 사용자 구현을 지정한다. `stream_transport`가 connector의 init/listen/connect/close 수명주기를 단독 관리한다. 별도로 connector를 초기화·listen·close하거나 여러 adapter에 같은 connector를 공유하지 않는다. 추가 shared 포인터는 초기화 전 구현별 설정과 진단 조회에 사용할 수 있지만 독립적인 수명주기를 허용하지 않는다. 인계하는 native stream과 adapter는 지정한 같은 루프를 사용해야 한다.

빈 connector는 `invalid_argument`를 던진다. 생성자는 할당 실패 예외를 던질 수 있다. `stream_transport_options`의 멤버는 다음과 같다.

| 멤버 | 기본값 | 의미 |
| --- | --- | --- |
| `connect_retry` | `1000` | 연결 실패 후 재시도까지의 밀리초. |
| `read_buffer_size` | `65536` | 양수이며 unsigned 최댓값 이하인 읽기 chunk 버퍼 크기. 잘못된 값은 `invalid_argument`를 던진다. |
| `limits` | `encoding_limits{}` | 디코딩할 헤더·payload·엔트리 수의 상한. 인코딩에도 적용한다. |
| `tracing` | 빈 tracer | 선택적인 수신 RPC 추적. |

## 수명주기와 완료

`init(id, address)`는 connector에 식별자·주소 설정을 위임한다. ID 0은 거부한다. 한 번만 호출하며 connector 초기화 실패 후에도 close가 필요하고 같은 adapter에서 다시 초기화할 수 없다. `start(receive)`는 listener와 완전한 메시지 전달을 등록한다. 초기화와 비어 있지 않은 콜백이 필요하며 성공한 start를 반복하면 `busy`다. listen/준비 실패는 종료를 시작한다. `uv_io`는 일반적인 노드 수명주기에서 이 메서드들을 호출한다.

`send(shared<const message>, completion)`에는 시작된 adapter와 0이 아닌 목적지 ID가 필요하다. 완료까지 메시지를 소유하고 native 쓰기 전에 인코딩한다. 즉시 오류이면 완료 콜백을 호출하지 않는다. 수락한 요청은 실패·취소를 포함해 정확히 한 번 완료한다. 송신 중 메시지 내용은 변경하지 않는다. 성공 완료는 바이트가 쓰였다는 뜻이며 원격 Raft 노드의 명령 commit을 의미하지 않는다.

목적지 ID/주소마다 송신 연결을 관리한다. 주소가 바뀌면 이전 연결을 종료하고 대기 요청을 취소한다. 연결 제출의 즉시 실패는 대기 요청을 제거하지 않고 재시도한다. 수락한 연결의 실패는 최신 3개만 남도록 가장 오래된 요청부터 `no_connection`으로 완료한 뒤 재시도한다. `set_connect_retry_delay(unsigned)`는 이미 활성화한 타이머를 다시 설정하지 않고 이후 재시도에 적용한다. 비동기 쓰기 실패는 `io_error`로 완료하고 해당 stream 종료 후 재연결한다. 연결 폐기 중 취소된 쓰기는 `canceled`다. native 쓰기가 즉시 거부되면 완료 콜백 없이 `io_error`를 반환하며, 이미 수락한 대기 큐를 비우는 도중의 쓰기 거부는 해당 요청을 `io_error`로 완료한다.

`close(callback)`은 수신을 중지하고 대기 쓰기를 취소하며 인계된 stream·타이머와 connector를 종료한다. 대기 연결과 쓰기는 종료 콜백 전에 완료한다. 수신 객체 준비에 실패한 stream도 native deleter 호출까지 유지한다. 비어 있지 않은 adapter 종료 콜백은 하나만 등록한다. 반복 종료는 멱등적이며 이미 종료했으면 콜백을 즉시 호출한다. 소멸자는 비동기 종료를 시작하고 루프를 동기 실행하지 않는다. 내부 상태는 wrapper 파괴 후에도 정리 완료까지 생존한다. adapter는 추가 할당이 없는 stream의 첫 observer 슬롯과 참조 callable을 사용해 콜백 저장소 할당 없이 종료를 시작한다.

## Framing·플러그인·오류

수신 chunk는 RPC를 임의로 나누거나 합칠 수 있다. adapter는 공개 [인코딩](encoding.md) 계약으로 16바이트 preamble, 상한이 있는 헤더·payload를 디코딩한다. 수신 메시지는 connector가 전달한 ID/주소를 소유한다. 잘못되거나 상한을 넘는 frame과 native 읽기 오류는 해당 수신 stream을 종료한다. 사용자 수신·송신 콜백의 예외는 경계에서 처리하고 adapter 종료를 시작한다. 실패 반환·완료 전에 native 쓰기·읽기 원인과 상대 문맥을 소유 `error_info`인 `error_details()`로 조회할 수 있다. 콜백 진입 시 값을 복사한다.

connector는 순서가 보존되고 바이트가 변환되지 않는 비차단 stream을 인계해야 한다. TTY를 제공한다면 채택 전에 적합한 raw 모드와 libuv 비차단 쓰기 설정(`uv_stream_set_blocking(handle, 0)` 등)을 준비한다. adapter는 바이너리 preamble·헤더·payload를 전송하며 터미널의 줄 처리나 문자 변환을 보정하지 않는다.

connector는 수락한 연결 콜백을 정확히 한 번 호출하고 종료 콜백 전에 대기 연결을 완료해야 한다. 연결·accept 콜백을 즉시 실행할 수 있으며 adapter는 재진입 중 상태를 유지한다. 연결의 즉시 오류는 완료 콜백을 호출하지 않는다. 성공 인계는 종료 중이 아니며 외부 native 연산이나 기존 종료 observer가 없는 유효한 stream을 제공해야 한다. deleter는 필요한 allocator·native owner를 connector와 독립적으로 유지해야 한다. connector만 종료했을 때 인계한 stream을 해제해서는 안 된다. connector 메서드·콜백은 같은 직렬 루프를 사용하며 close는 예외를 던지지 않는다. native 핸들의 소유권은 [raw 연결 계약](uv_stream.md)을 참고한다.
