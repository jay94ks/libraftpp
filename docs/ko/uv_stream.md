# 소유 libuv stream과 연결 transport

[English](../en/uv_stream.md) · [TCP 메시지 전송](tcp_transport.md) · [libuv I/O](uv_io.md) · [인코딩](encoding.md) · [메시지 adapter](stream_transport.md) · [모듈별 API](modules/index.md)

`<raftpp/uv/uv_stream.hpp>`, `<raftpp/uv/uv_stream_transport.hpp>`를 포함한다. `<raftpp.hpp>`에서도 제공한다. `RAFTPP_WITH_UV=ON`이 필요하며 fixture·LZ4는 독립적이다. 호출과 콜백은 소유 libuv loop에서 직렬화하며 모든 종료 완료까지 loop가 살아 있어야 한다.

## Stream 소유권

`uv_stream`은 초기화된 native TCP·pipe·TTY stream 하나를 소유하는 이동 전용 값이다. C++ 값을 이동해도 native 핸들의 주소는 유지된다. 기본 생성은 빈 소유자를 만든다. `operator bool`은 종료를 시작하지 않은 소유 stream을 나타내며 연결 상태나 읽기 가능 여부를 보장하지 않는다.

```cpp
uv_stream(uv_stream_s& stream, stream_deleter release);
borrowed<uv_stream_s> native_handle() const noexcept;
error_code read_start(stream_read_callback callback, size_t buffer_size = 65536) noexcept;
error_code read_stop() noexcept;
error_code write(view<const shared_buffer> buffers, completion callback = {}) noexcept;
error_code write(shared_buffer bytes, completion callback = {}) noexcept;
error_code close(close_callback callback = {}) noexcept;
error_info error_details() const noexcept;
```

채택하려는 native stream은 초기화되었고 비활성·비종료 상태여야 하며 deleter가 비어 있지 않아야 한다. 생성에 성공한 경우에만 소유권을 이전하고 예외 시에는 호출자가 원래 할당을 계속 소유한다. 소유 중 native `data`와 읽기·쓰기·종료는 uv_stream이 관리한다. 대여 핸들로 호환되는 native 설정·조회(TTY의 창 크기 조회 등)를 수행할 수 있지만 data를 바꾸거나 핸들을 이동·해제하거나 native 읽기·쓰기·종료를 별도로 호출하면 안 된다. native 종료 뒤 deleter를 호출하기 전에 원래 data를 복원한다. deleter는 원래 할당을 해제하며 예외를 던지면 안 된다. 플러그인은 이미 파괴되었을 수 있는 connector를 대여하는 대신 해제에 필요한 allocator/owner를 캡처해야 한다.

native_handle()은 대여 값이며 종료 완료 뒤 null이 된다. 소멸하거나 기존 소유자 위로 이동 대입하면 비동기 종료를 시작한다. 내부 소유권은 libuv가 연산을 완료·취소하고 핸들을 닫을 때까지 native 메모리와 요청 버퍼를 유지한다. loop 실행은 호출자의 책임이다.

## 읽기와 쓰기

stream_read_callback은 function<void(error_code, shared_buffer)>다. 성공한 읽기는 독립 소유 chunk를 제공하며 fragment/coalesce될 수 있다. chunk는 콜백이나 stream 종료 뒤에도 유효하다. 읽기 버퍼 크기는 양수이며 unsigned 최댓값 이하여야 한다. 두 번째 활성 구독은 busy를 반환하고 read_stop()은 구독을 제거한다. EOF는 native libuv 원인을 포함한 no_connection으로 알린다. 읽기 오류는 구독을 중지하고 이후 읽기 재시작은 거부한다. native stream이 지원하면 쓰기 방향은 계속 사용할 수 있다. 종료하면 읽기 알림을 중지한다.

TTY 바이트는 호출자가 선택한 native 터미널 모드의 영향을 받는다. `uv_stream`의 채택·이동은 모드를 선택하거나 텍스트 변환을 추가하지 않는다. NUL·CR·LF를 포함한 바이너리 데이터를 그대로 전송하려면 채택 전에 적합한 raw 모드를 구성한다. 모드·창 크기 등의 native 설정은 호출자의 책임이며, 소유 중 native 읽기·쓰기·종료를 별도로 실행해서는 안 된다는 규칙은 그대로 적용된다.

단방향 native pipe에서는 해당 끝이 지원하는 방향만 사용할 수 있다. 읽기 전용 끝에 쓰기를 제출하면 `io_error`를 즉시 반환하고 완료 콜백을 호출하지 않는다. `error_details()`는 libuv가 보고한 원인을 보존한다. pipe의 EOF도 일반 stream과 같은 읽기 종료 계약을 따르며 콜백 안에서 `close()`를 호출할 수 있다.

쓰기는 비어 있지 않은 각 버퍼의 공유 소유권을 완료까지 유지한다. 하나 이상의 버퍼가 비어 있지 않아야 하며 버퍼 개수와 개별 크기는 libuv unsigned 한계 안에 있어야 한다. 성공은 수락을 뜻하고 종료 중 취소를 포함해 정확히 한 번 완료한다. 즉시 오류에서는 콜백을 호출하지 않는다. 입력 배열 자체는 호출 중에만 유지하면 되며 전송 중 버퍼 내용은 변경하지 않는다.

close()는 읽기를 중지하고 수락된 쓰기를 취소·정리한 뒤 native 할당 deleter와 등록한 종료 콜백을 순서대로 호출한다. 여러 종료 관찰자를 등록할 수 있다. 첫 번째 비어 있지 않은 관찰자는 전달할 callable 자체를 생성하는 것 외에 추가 할당 없이 저장한다. 이후 관찰자는 저장소를 할당할 수 있으며 실패하면 out_of_memory를 반환하고 해당 관찰자를 등록하지 않는다. 이미 시작한 종료는 유지한다. 인자 없는 close도 관찰자 할당 없이 동작한다. 빈 값·이미 종료된 stream은 종료 콜백을 인라인 호출한다. loop 콜백 안에서 같은 loop의 종료 완료를 동기 대기하지 않는다.

native 경계를 넘어 콜백 예외를 전파하지 않는다. 읽기·쓰기 완료 콜백이 예외를 던지면 invalid를 기록하고 종료한다. 진단은 메시지를 소유하고 native 원인을 보존하지만 조회가 대여 native 핸들의 수명을 연장하지는 않는다.

## Raw 연결 인터페이스

uv_stream_transport는 연결 수준의 플러그인 인터페이스다.

```cpp
error_code init(server_id id, string_view address);
error_code listen(stream_accept_callback callback);
error_code connect(server_id id, string_view address, stream_connect_callback callback);
void close(close_callback callback = {}) noexcept;
```

stream_accept_callback은 function<void(server_id, string, uv_stream)>이며 식별자·주소·stream을 소유 값으로 인계한다. stream_connect_callback은 function<void(error_code, uv_stream)>으로 성공 시 stream을, 실패 시 빈 소유자를 제공한다. 즉시 연결 거부는 콜백을 호출하지 않고 수락된 요청은 한 번 완료한다. connector 종료는 accept를 멈추고 진행 중인 연결 수립을 취소하며 이 요청들을 완료한 뒤 connector 종료 콜백을 호출한다. 비어 있지 않은 connector 종료 관찰자는 하나만 등록한다. 인계한 stream은 독립적이므로 소유자가 닫아야 하며 connector 종료가 이를 기다리지는 않는다.

tcp_stream_transport(loop, tcp_transport_options)는 IPv4 DNS 해석·여러 주소 시도와 cowsql/raft version-1 little-endian 식별자/주소 handshake를 구현한다. tcp_transport와 TCP 연결 수립 구현을 공유한다. listen/connect 전에 init을 한 번 호출하며 connect는 listen 없이도 가능하다. 해석한 주소를 한 차례씩 시도하고 비동기 실패 시 no_connection으로 완료하며 무한 재시도하지 않는다. 대상 ID는 진단 문맥이며 원격 신원 인증이 아니다. native 거부·해석·handshake 원인은 완료 전에 error_details()로 제공한다.

set_bind_address는 init 전에 호출한다. 기본 포트·bind 정책·max_handshake_address는 [TCP 메시지 전송](tcp_transport.md)과 같다. 수신 handshake에는 0이 아닌 sender ID, 올바른 padded 주소 길이, 종료 NUL과 0 padding이 필요하다. 종료하면 미완료 handshake와 DNS/connect/write 요청을 취소한다. 이미 인계한 stream은 connector 객체가 파괴된 뒤에도 사용할 수 있다.

uv_io의 인자는 완성된 Raft 메시지를 제공하는 shared<transport>다. raw connector를 [stream_transport](stream_transport.md)로 감싸 framing·큐·재시도를 이 backend에 연결한다. native stream chunk에는 RPC 경계가 암묵적으로 존재하지 않는다.

## 수신 연결 유지

아래 함수는 가장 최근에 받은 연결을 호출자가 소유하는 공유 상태에 유지한다. 콜백의 캡처는 함수가 반환된 뒤에도 유지된다. 연결이 필요한 동안 상태 소유자를 유지하고 종료까지 loop를 실행한다.

```cpp
#include <raftpp/uv/uv_stream_transport.hpp>
#include <cstdio>

raftpp::error_code listen_for_one(
    raftpp::tcp_stream_transport& listener,
    raftpp::shared<raftpp::optional<raftpp::uv_stream>> accepted)
{
    namespace r = raftpp;
    if (auto error = listener.init(1, "127.0.0.1:8080")) return error;
    return listener.listen([accepted = std::move(accepted)](
        r::server_id, r::string, r::uv_stream stream) {
        accepted->emplace(std::move(stream));
        auto error = (*accepted)->read_start([](r::error_code status, r::shared_buffer data) {
            if (status) return;
            std::fwrite(data.bytes().data(), 1, data.size(), stdout);
        });
        if (error) (void)(*accepted)->close();
    });
}
```

native 수명은 libuv의 [handle](https://docs.libuv.org/en/v1.x/handle.html)과 [stream](https://docs.libuv.org/en/v1.x/stream.html) 계약을 따른다.
