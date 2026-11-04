# 스트림·Framing·실행기·코덱

[English](../en/streams.md) · [모듈별 API](modules/index.md)

필수 타깃 `raftpp::core`는 `<raftpp/stream.hpp>`, `<raftpp/framed_transport.hpp>`, `<raftpp/executor.hpp>`, `<raftpp/compression.hpp>`를 제공한다. 선택 [어댑터 모듈](dependencies.md)이 이 추상 인터페이스를 구현한다. 타입은 `raftpp` 네임스페이스에 있다.

## 바이트 스트림

`byte_stream`은 연결된 순서 보장·신뢰성 있는 양방향 스트림 하나를 나타낸다. `read_start(stream_read_callback, size_t buffer_size = 65536)`는 소유권이 있는 `shared_buffer` 조각 수신을 시작한다. 조각 크기는 쓰기 경계와 다를 수 있으며 `buffer_size`는 읽기 할당 힌트다. 비어 있지 않은 콜백과 0이 아닌 크기가 필요하다. 스트림 끝은 `errc::no_connection`으로 알린다. `read_stop()`은 읽지 않은 네이티브 데이터를 버리지 않고 알림을 중지한다.

`write(view<const shared_buffer>, completion = {})`는 버퍼를 순서대로 쓴다. 단일 버퍼 오버로드도 같은 계약이다. 즉시 제출 오류이면 완료 콜백을 호출하지 않는다. 접수한 쓰기는 데이터를 유지하고 정확히 한 번 완료한다. 성공은 로컬 SDK의 접수/완료이며 원격 애플리케이션 전달이나 Raft commit을 의미하지 않는다. 다른 쓰기가 진행 중이면 백엔드가 `busy`를 반환할 수 있다. 입력 view 자체의 저장소는 제출 후 파괴해도 된다.

`close(close_callback = {})`는 읽기를 중지하고 접수한 작업을 완료/취소한 뒤 종료 완료를 알린다. 소멸자는 종료를 시작하며 동기 대기하지 않는다. 종료 완료까지 네이티브 실행 영역을 계속 구동한다. Asio/mvfst/MsQuic 스트림의 대기 중인 종료 observer는 하나이며 추가 nonempty observer는 `busy`다. UV 스트림은 여러 observer를 지원하며 추가 등록은 할당할 수 있다. 사용자 콜백은 예외를 던지면 안 된다. `error_details()`는 값을 소유하는 진단을 반환한다.

| 구현체 | 생성과 소유권 | 실행 영역 |
| --- | --- | --- |
| `uv_stream` | 초기화된 네이티브 핸들과 지정 deleter를 소유 | 해당 libuv 루프 |
| `asio_stream` | 연결된 `asio::ip::tcp::socket`을 값/이동으로 인수 | 소켓 executor. 단일 스레드 또는 strand로 직렬화 |
| `mvfst_stream` | `shared<quic::QuicSocketLite> connection, quic::StreamId stream`. 연결된 양방향 스트림의 콜백을 독점 소유 | 연결의 event-base 스레드 |
| `msquic_stream` | 공유 API 테이블·연결된 `HQUIC` 스트림·`shared<void> connection_owner`. 생성 성공 후 네이티브 스트림/콜백 소유권 인수 | 연결의 직렬화된 MsQuic 콜백 실행 영역 |

이벤트 루프는 네이티브 종료보다 오래 살아 있어야 한다. `open_msquic_api()`는 `MsQuicClose`로 해제하는 공유 테이블을 반환하고 SDK 실패 시 `system_error`를 던진다. MsQuic의 연결 owner는 connection과 registration을 유지해야 한다. 인수한 스트림에 경쟁하는 네이티브 콜백을 설치하거나 외부에서 닫지 않는다. MsQuic 수신 버퍼는 SDK에 반환하기 전에 복사하고 송신 버퍼는 `SEND_COMPLETE`까지 유지한다. mvfst 쓰기는 SDK가 소유하는 `IOBuf`에 복사하며 `writeChain`의 복사본 접수 직후 성공 콜백을 인라인으로 실행할 수 있다. mvfst 종료는 reader를 분리하고 해당 스트림만 reset하며 공유 연결의 다른 스트림은 유지한다. Asio는 소켓을 값으로 인수하므로 이후 생성 실패 시에도 인수한 소켓을 정리한다. 생성자는 할당 예외를 던질 수 있다. UV/QUIC의 native 스트림 인계는 생성 성공 후 성립하며 Asio의 값 인자 소켓은 실패 시에도 소비된다.

어댑터는 네이티브 실행 영역을 유지한다. 이벤트 루프 사이의 자동 콜백 전달이나 Raft 노드 동기화를 제공하지 않는다. 하나의 transport로 조립하는 모든 스트림은 노드 I/O 콜백과 같은 직렬화 실행 영역을 사용해야 한다. 서로 다른 네이티브 worker 연결은 Raft core 호출 전에 애플리케이션 bridge가 필요하다. QUIC의 TLS/listener/연결 설정과 peer 신원 검증은 connector가 담당한다.

## Raft Framing

`framed_transport(encoding_limits = {})`는 core의 `transport`를 구현한다. `attach(server_id, string_view address, shared<byte_stream>)`로 `start` 이전에 인증한 peer를 등록한다. peer ID와 주소는 0/빈 값이면 안 된다. 중복 ID는 `duplicate_id`, 여러 peer의 같은 스트림 공유는 `busy`다. transport가 등록된 각 스트림의 읽기/종료 수명주기를 독점 관리한다. attach의 할당 실패는 기존 상태를 보존한다.

`init(local_id, local_address)`와 `start(function<void(message)>)`를 각각 한 번 호출한다. 송신은 메시지의 대상 `server_id`를 사용한다. 없거나 실패한 peer는 `no_connection`이다. 접수된 송신 완료는 하위 스트림의 계약을 따른다. 수신 메시지는 등록된 peer의 ID/주소를 받으며 기존 Raft wire encoding을 사용한다. parser는 임의 조각과 여러 프레임을 처리하고 버퍼 확장 전에 header/payload/entry 제한을 적용한다. 잘못된 프레임은 해당 peer의 reader를 중단하고 진단을 기록한다. 종료는 등록한 모든 스트림을 정리한 뒤 완료한다.

connector가 연결된 스트림을 제공한다. 이 transport는 주소 해석·TLS 연결 수립·peer 인증·재접속을 수행하지 않는다. 실행 영역이 일치하면 I/O 백엔드의 `shared<transport>` 인자로 전달할 수 있다.

```cpp
#include <raftpp/asio/asio.hpp>
#include <raftpp/framed_transport.hpp>

// socket은 연결되어 있고 노드의 직렬화 실행 영역에 속한다.
auto stream = std::make_shared<raftpp::asio_stream>(std::move(socket));
auto network = std::make_shared<raftpp::framed_transport>();
auto error = network->attach(2, "peer-2", stream);
// error 확인 후 I/O 백엔드가 network의 init/start/close 수명주기를 관리한다.
```

## 콜백 실행기

`executor::post(function<void()>) noexcept`는 한 번의 나중 호출을 예약한다. 빈 콜백은 `invalid`이며 실패 시 콜백 호출 없이 오류를 반환한다. 사용자 콜백은 예외를 던지면 안 된다. post 성공 시 callable은 네이티브 queue로 이전된다. 접수한 작업이 끝날 때까지 queue를 구동하며 코루틴/future API는 제공하지 않는다.

`asio_executor(asio::any_io_executor)`는 네이티브 `asio::post`를 사용한다. `asyncpp_executor(shared<asyncpp::dispatcher>)`는 dispatcher의 `push`를 사용하고 대기 콜백을 처리할 때까지 구체 dispatcher를 유지한다. 상류 base에 public virtual 소멸자가 없으므로 공유 owner는 구체 dispatcher 타입으로 생성한다. null dispatcher는 `invalid_argument`다. 각 상류 실행기의 스레드/queue 계약을 따른다.

## 압축

`compression_codec`은 동기 메서드 `compress(view<const byte>, buffer&) const noexcept`, `decompress(view<const byte>, buffer&, size_t max_size) const noexcept`를 제공한다. `lz4_codec`, `zstd_codec`, `zlib_codec`은 각각 LZ4 frame, Zstandard frame, zlib-wrapped DEFLATE로 구현한다.

각 decoder는 정확히 하나의 완전한 frame만 허용하고 잘린 입력/후행 바이트를 거부하며 `max_size`보다 크게 확장하지 않는다. 초과 확장은 `too_big`, 잘못된 데이터는 `corrupt`, 할당 실패는 `out_of_memory`다. 입력이 출력과 겹쳐도 오류 시 출력과 allocator를 보존한다. 빈 원본도 압축할 수 있지만 빈 인코딩 입력의 해제는 실패한다. 올바른 빈 frame은 `max_size = 0`으로 해제할 수 있다. 코덱은 상태가 없고 호출마다 독립 네이티브 context를 만든다.

```cpp
#include <raftpp/zstd/zstd.hpp>

raftpp::zstd_codec codec;
raftpp::buffer encoded, decoded;
auto error = codec.compress(data, encoded);
if (!error) error = codec.decompress(encoded.bytes(), decoded, 8 * 1024 * 1024);
```

코덱을 선택해도 기존 디스크/네트워크 형식이나 스냅샷 압축 설정은 바뀌지 않는다. UV의 기존 LZ4 스냅샷 decoder는 이 frame API와 독립적으로 호환 계약을 유지한다.
