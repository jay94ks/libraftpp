# uv 모듈 API

[English](../../en/modules/uv.md) · [모듈 목록](index.md)

`RAFTPP_WITH_UV=ON`에서 `raftpp::uv`와 `<raftpp/uv/uv.hpp>`를 제공한다. Linux, libuv 1.18.0 이상과 필요한 Linux 헤더가 필요하다. core는 전이적으로 연결된다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS uv)
target_link_libraries(app PRIVATE raftpp::uv)
```

## 타입별 API

| 공개 헤더 | 타입·역할 | 상세 계약 |
| --- | --- | --- |
| `uv_io.hpp` | `uv_io : io`, `uv_io_options`; 파일 영속화·로그·snapshot·타이머 | [I/O API](../uv_io.md) |
| `tcp_transport.hpp` | `tcp_transport : transport`, `tcp_transport_options`; IPv4 TCP와 RPC | [TCP API](../tcp_transport.md) |
| `uv_stream.hpp` | `uv_stream : byte_stream`, `stream_deleter`; TCP·pipe·TTY native handle 소유 | [스트림 API](../uv_stream.md) |
| `uv_stream_transport.hpp` | `uv_stream_transport`, `tcp_stream_transport`, accept/connect 콜백 | [연결 API](../uv_stream.md) |
| `stream_transport.hpp` | `stream_transport : transport`, `stream_transport_options`; raw connector 위 framing·큐·재시도 | [메시지 어댑터](../stream_transport.md) |
| `compression.hpp` | `is_compressed`, `decompress`; LZ4 snapshot 판별·읽기 | 아래 압축 보조 함수 |

헤더 경로는 모두 `<raftpp/uv/...>`다. `uv_stream`은 이동 전용이며 기본 생성 시 비어 있다. 다른 위 구체 I/O/transport 객체는 복사할 수 없다. `uv_stream_transport`는 사용자 connector가 구현하는 추상 인터페이스다.

## 소유권과 종료

`uv_io`와 TCP/stream transport는 libuv loop를 대여한다. loop는 모든 종료 완료까지 살아 있고 구동되어야 한다. `uv_io`는 전달한 `shared<transport>`를 공동 소유하며, `stream_transport`는 전달한 connector의 init/listen/connect/close 수명을 독점한다. 콜백을 포함한 모든 호출은 같은 loop에서 직렬 실행한다.

`uv_stream`은 초기화된 inactive native stream과 해제 함수를 받아 성공적인 생성 뒤 소유권을 가져간다. 실패하면 native handle 소유권은 호출자에게 있다. native `data`와 read/write/close는 라이브러리가 제어한다. `native_handle()`은 대여 포인터이고 자체 read/write/close·메모리 해제에 사용하면 안 된다. native close 뒤 원래 `data`를 복원하고 deleter를 실행한다.

수락된 쓰기·디스크 요청은 완료까지 데이터를 유지한다. 즉시 제출 실패는 해당 완료 콜백을 호출하지 않는다. `close`/파괴는 비동기 정리를 시작하며 loop에서 동기 대기하면 안 된다. `uv_stream::close`는 여러 observer를 지원하고 추가 observer 등록은 할당할 수 있다. I/O/connector/메시지 transport는 각 세부 문서의 단일 observer 정책을 따른다. tick·receive·완료·deleter는 예외를 던지지 않도록 작성한다.

## 압축 보조 함수

```cpp
bool is_compressed(view<const byte> input) noexcept;
error_code decompress(view<const byte> input, buffer& output,
                      size_t max_size = numeric_limits<size_t>::max()) noexcept;
```

`is_compressed`는 첫 4바이트 `04 22 4d 18`만 확인하며 프레임 유효성 검사가 아니다. LZ4가 비활성화되어도 이 판별은 제공한다.

`decompress`는 LZ4 첫 프레임의 payload를 동기 복원한다. `max_size`는 복원 바이트 상한이며 제한 초과는 `too_big`, 잘못되거나 잘린 입력은 `io_error`, 할당 실패는 `out_of_memory`다. 실패 시 기존 출력은 유지한다. 성공 시 첫 프레임 뒤 추가 바이트를 거부하지 않으며 출력 allocator 보존을 보장하지 않는다. 정확히 한 프레임과 출력 allocator 보존이 필요하면 [LZ4 코덱](lz4.md)을 사용한다.

`RAFTPP_WITH_LZ4=OFF`이면 `decompress`는 입력과 관계없이 `invalid`다. `RAFTPP_WITH_LZ4=ON`은 UV와 liblz4 1.7.1 이상이 필요하다. 별도 `RAFTPP_MODULE_LZ4`는 UV 없이 선택 가능한 코덱 모듈이다. 두 옵션이 켜지면 UV는 그 모듈의 LZ4 링크 의존성을 사용한다. `uv_io::set_snapshot_compression(bool)`은 성공하는 no-op이며 쓰기 압축을 활성화하지 않는다.

## 사용 예제

```cpp
#include <raftpp/uv/uv.hpp>
#include <uv.h>

raftpp::shared<raftpp::uv_io> make_backend(uv_loop_t& loop,
                                        raftpp::fs::path directory)
{
    auto network = std::make_shared<raftpp::tcp_transport>(loop);
    return std::make_shared<raftpp::uv_io>(loop, std::move(directory),
                                         std::move(network));
}
```

디렉터리는 미리 생성하고 읽기·쓰기 가능해야 한다. 반환한 객체를 노드에 `shared<io>`로 전달하면 노드가 초기화·load·시작·종료를 호출한다. loop는 노드 종료까지 유지한다. 생성자는 할당 예외를 던질 수 있다. 설정 오류·native 실패의 반환 코드와 `error_details()`는 [I/O](../uv_io.md), [TCP](../tcp_transport.md), [오류](../errors.md)에서 설명한다.
