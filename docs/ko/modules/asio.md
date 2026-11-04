# asio 모듈 API

[English](../../en/modules/asio.md) · [모듈 목록](index.md)

`RAFTPP_MODULE_ASIO=ON`이면 `raftpp::asio`와 `<raftpp/asio/asio.hpp>`를 제공한다. standalone Asio를 사용하며 core, Threads와 `ASIO_STANDALONE` 사용 조건이 타깃에서 전달된다. Boost.Asio 타입을 인자로 전달하지 않는다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asio)
target_link_libraries(app PRIVATE raftpp::asio)
```

## 실행기

```cpp
explicit asio_executor(asio::any_io_executor executor);
error_code post(function<void()> callback) noexcept override;
```

실행기 값을 보관하고 `asio::post`로 콜백을 나중에 한 번 실행한다. 빈 콜백은 `invalid`, 제출 중 할당 실패는 `out_of_memory`, 다른 제출 예외는 `io_error`다. 즉시 실패 시 콜백을 실행하지 않는다. 수락된 작업은 native 큐에 속하며 adapter 파괴로 취소되지 않는다. executor 값 보관이 `io_context`의 수명을 연장하지는 않으므로 호출자는 context를 유지하고 큐를 구동한다. 콜백은 예외를 던지지 않아야 한다.

```cpp
#include <raftpp/asio/asio.hpp>
#include <asio/io_context.hpp>

int main()
{
    asio::io_context context;
    raftpp::asio_executor scheduler{context.get_executor()};
    int count = 0;
    if (auto error = scheduler.post([&count] { ++count; }))
        return 1;
    context.run();
    return count == 1 ? 0 : 1;
}
```

## TCP 스트림 생성과 소유권

```cpp
explicit asio_stream(asio::ip::tcp::socket socket);
```

이미 연결된 소켓을 `std::move`로 넘긴다. 소켓은 값 인자에서 소비되므로 생성 실패 시에도 호출자에게 복원되지 않는다. 닫힌 소켓은 `std::invalid_argument("closed asio socket")`, 상태 할당 실패는 할당 예외를 던진다. 열려 있다는 검사만으로 연결 여부를 보장하지 않으므로 연결 수립은 호출자 책임이다. `asio_stream`은 복사/이동할 수 없고 필요하면 `shared<asio_stream>`로 소유한다.

모든 메서드·파괴·콜백은 소켓 executor의 같은 직렬 실행 영역에서 실행한다. context를 한 스레드로 구동하거나 소켓 executor를 strand로 구성하고 호출도 그 strand에 제출한다. adapter는 자체 strand를 만들지 않는다. native socket은 adapter가 소유하며 외부에서 직접 read/write/close하지 않는다.

## 스트림 메서드

아래 메서드는 모두 `noexcept`다. 연산은 성공 시 빈 `error_code`를 반환하며 `error_details()`는 `error_info`를 반환한다. 기본 `buffer_size`는 65536이다.

| 메서드 | 입력과 동작 | 주요 실패 |
| --- | --- | --- |
| `read_start(stream_read_callback callback, size_t buffer_size = 65536)` | 읽기 구독 시작. 성공 데이터는 소유한 `shared_buffer`. 중지 후 재개 시 보류 데이터를 즉시 전달할 수 있다. | 빈 콜백·크기 0은 `invalid`, 기존 구독은 `busy`, 종료/영구 읽기 오류 뒤에는 `shutdown` |
| `read_stop()` | 현재 구독을 제거하고 해당 읽기만 취소한다. 쓰기는 유지하며 이미 소비된 native 바이트는 재개까지 보관한다. | 정상 중지·종료 상태에서는 성공 |
| `write(view<const shared_buffer> buffers, completion callback = {})` | 배열 순서대로 전체 버퍼를 전송한다. 버퍼 owner는 완료까지 유지한다. 빈 쓰기도 허용한다. | 진행 중 쓰기는 `busy`, 종료 중에는 `shutdown` |
| `write(shared_buffer bytes, completion callback = {})` | 상속한 단일 버퍼 편의 오버로드. 같은 계약을 적용한다. | 배열 쓰기와 동일 |
| `close(close_callback callback = {})` | 읽기 중단·native socket close 뒤 수락된 작업을 drain/cancel하고 완료한다. | 아직 실행되지 않은 close observer가 있으면 새 nonempty observer에 `busy` |
| `error_details() const` | 최근 기록한 native 오류의 소유 진단을 반환한다. | 오류를 반환하는 모든 경로가 별도 진단을 갱신하는 것은 아니다. |

할당 실패는 `out_of_memory`, native 또는 기타 제출 실패는 `io_error`로 보고할 수 있다. native EOF/reset/broken pipe는 `no_connection`, operation_aborted는 `canceled`로 매핑한다. native 원인은 기록된 `error_info::cause`에 보존한다. 즉시 쓰기 실패에는 완료 콜백이 없고, 수락된 쓰기는 한 번 완료한다. 버퍼 배열 자체는 제출 뒤 해제할 수 있지만 데이터는 변경하지 않는다. 완료는 로컬 Asio 쓰기 완료이며 원격 ACK/commit이 아니다.

읽기 구독의 오류는 해당 읽기를 중단한다. 수신 fragment는 쓰기/RPC 경계와 무관하다. 읽기·쓰기 콜백에서 close/adapter 파괴를 요청해도 내부 상태는 보존하고 활성 콜백이 반환한 뒤 종료 완료를 알린다. 대기 작업이 없거나 이미 닫혔다면 close 콜백은 즉시 실행할 수 있다. 소멸자는 close를 시작하며 동기 대기하지 않는다. 모든 사용자 콜백은 예외를 던지지 않아야 한다.

## Raft transport 조립

```cpp
#include <raftpp/asio/asio.hpp>
#include <raftpp/framed_transport.hpp>

raftpp::shared<raftpp::framed_transport> make_asio_transport(
    asio::ip::tcp::socket connected_socket)
{
    auto stream = std::make_shared<raftpp::asio_stream>(std::move(connected_socket));
    auto network = std::make_shared<raftpp::framed_transport>();
    if (auto error = network->attach(2, "peer-2", std::move(stream)))
        throw raftpp::system_error(error);
    return network;
}
```

호출자는 연결·peer 인증을 완료하고 소켓과 노드 I/O의 실행 영역을 일치시킨다. 반환한 transport의 init/start/close는 I/O 백엔드에 맡긴다. UV I/O와 다른 context를 직접 연결하는 자동 bridge는 없다. 별도 영속 I/O 백엔드나 TCP listener는 이 모듈의 API에 포함되지 않는다. [공통 스트림 계약](../streams.md)을 함께 따른다.
