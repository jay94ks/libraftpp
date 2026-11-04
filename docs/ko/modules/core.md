# core 모듈 API

[English](../../en/modules/core.md) · [모듈 목록](index.md) · [I/O 조립](../composition.md)

`raftpp::core`는 항상 활성화되는 필수 라이브러리다. `<raftpp/raft.hpp>`는 core 공개 API를 모으며 타입은 `<raftpp/common.hpp>`의 표준 별칭과 소유권 의미를 사용한다. 외부 네트워크 SDK 없이 사용할 수 있다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS core)
target_link_libraries(app PRIVATE raftpp::core)
```

## 공개 API 안내

| 헤더 | 주요 API | 계약 설명 |
| --- | --- | --- |
| `core.hpp`, `types.hpp` | `core_state`, 이벤트·업데이트, `step`, 상태/리더 조회 | [이벤트 코어](../core.md) |
| `node.hpp`, `io.hpp`, `state_machine.hpp` | `node`, `io`, `state_machine`, 요청과 콜백 | [노드 API](../node.md) |
| `configuration.hpp` | `configuration`, 멤버 ID·주소·역할 | [설정](../configuration.md) |
| `buffer.hpp`, `entry.hpp`, `snapshot.hpp` | `buffer`, `aligned_buffer`, `shared_buffer`, 배치·snapshot | [값과 소유권](../storage.md) |
| `common.hpp`, `memory.hpp` | 표준 별칭, `owned`/`shared`/`borrowed`/`view`, allocator | [메모리](../memory.md) |
| `message.hpp`, `encoding.hpp`, `checksum.hpp` | RPC와 디스크 직렬화·크기 한도, CRC32·SHA-1 | [인코딩](../encoding.md) |
| `error.hpp` | `errc`, `error_code`, 소유 진단 `error_info` | [오류](../errors.md) |
| `tracing.hpp` | `tracer`, 이벤트 관찰 | [추적](../tracing.md) |
| `stream.hpp`, `transport.hpp`, `framed_transport.hpp` | 바이트 스트림·메시지 transport와 framing | [스트림과 framing](../streams.md) |
| `executor.hpp`, `compression.hpp` | 콜백 실행기·동기 프레임 코덱의 추상 인터페이스 | [실행기와 코덱](../streams.md) |
| `fixture.hpp` | 가상 시간 cluster fixture | [fixture](../fixture.md), `RAFTPP_WITH_FIXTURE=ON`에서만 제공 |

공개 헤더의 `detail/` 지원 타입은 공개 값 타입의 정의에 사용한다. 직접 조립하는 사용자는 위 진입 API를 사용한다.

## 공통 추상 인터페이스

```cpp
using stream_read_callback = function<void(error_code, shared_buffer)>;

error_code executor::post(function<void()> callback) noexcept;
error_code compression_codec::compress(view<const byte> input, buffer& output) const noexcept;
error_code compression_codec::decompress(view<const byte> input, buffer& output,
                                       size_t max_size) const noexcept;
```

위 선언은 `raftpp` 네임스페이스의 API 서명이다. `post`는 수락한 콜백을 나중에 한 번 실행한다. 빈 콜백은 `invalid`다. 구체 실행기의 큐를 계속 구동해야 하며 콜백은 예외를 던지지 않아야 한다. 코덱은 동기 연산이고 `max_size`는 압축 해제된 바이트의 상한이다. 실패 시 출력과 allocator를 유지한다. 구체 구현은 각 선택 모듈에 있다.

`byte_stream`은 `read_start`, `read_stop`, 두 `write` 오버로드, `close`, `error_details`를 제공한다. 읽기 데이터는 소유한 `shared_buffer`이며 분할 단위는 메시지 경계와 다르다. 구체 스트림의 `write` 완료 시점과 close observer 정책은 각 모듈 문서를 따른다.

## `framed_transport`

| 메서드 | 입력·반환·상태 계약 |
| --- | --- |
| `framed_transport(encoding_limits limits = {})` | 생성 시 할당할 수 있다. parser·encoder에 같은 한도를 적용한다. 복사할 수 없다. |
| `attach(server_id peer, string_view address, shared<byte_stream> stream)` | `start` 전 등록. ID 0·빈 주소·null은 `invalid`, ID 중복은 `duplicate_id`, 동일 스트림 재사용 또는 시작 뒤 등록은 `busy`. 주소를 복사하고 스트림을 공동 소유한다. 할당 실패는 `out_of_memory`. |
| `init(server_id id, string_view address)` | ID 0·빈 주소는 `invalid`, 반복 초기화는 `busy`. |
| `start(function<void(message)> receive)` | 초기화 전 또는 빈 콜백은 `invalid`, 반복 시작은 `busy`. 등록 스트림의 읽기를 시작한다. |
| `send(shared<const message> value, completion callback)` | `value->server_id`로 peer 선택. 시작 전·null은 `invalid`, 없는/실패한 peer는 `no_connection`. 수락한 메시지를 완료까지 유지한다. |
| `close(close_callback callback = {}) noexcept` | 읽기를 멈추고 등록 스트림을 닫은 뒤 완료한다. 대기 중인 첫 비어 있지 않은 close observer를 유지한다. 완료 후 다시 제공한 콜백은 즉시 실행한다. |
| `error_details() const noexcept` | 복사 가능한 소유 진단. 잘못된 프레임은 해당 peer의 읽기를 중단한다. |

종료가 시작된 뒤 `attach`·`init`·`start`·`send`는 `shutdown`을 반환한다. `error_code` 연산은 성공 시 빈 코드를 반환한다. encode/parse 오류는 [인코딩](../encoding.md)의 코드를 따른다. 생성자는 할당 예외를 던질 수 있고, 이후 public 연산은 헤더에 선언된 `noexcept` 경계를 따른다. 콜백은 예외를 던지지 않아야 한다.

transport는 스트림의 읽기·쓰기·종료를 독점한다. peer 인증과 연결 수립을 완료한 뒤 등록하고 직접 native 작업을 병행하지 않는다. 수신 메시지의 peer ID/주소는 등록값이다. 다른 이벤트 루프 사이의 자동 전달은 없다. I/O 백엔드에 transport를 넘겼다면 그 백엔드가 `init`·`start`·`close`를 호출하도록 한다.

## 사용 예제

```cpp
#include <raftpp/framed_transport.hpp>

raftpp::shared<raftpp::framed_transport> make_transport(
    raftpp::shared<raftpp::byte_stream> connected_peer)
{
    auto network = std::make_shared<raftpp::framed_transport>();
    if (auto error = network->attach(2, "peer-2", std::move(connected_peer)))
        throw raftpp::system_error(error);
    return network;
}
```

호출자는 인증된 연결과 같은 직렬 실행 영역의 스트림을 제공하고, 반환한 transport를 I/O 백엔드에 연결한다. 노드·로그·설정의 수명/영속화 순서는 위 세부 문서의 계약을 따른다.

`version.hpp`의 `reference_version`/`reference_version_number`는 알고리즘·형식의 출처 버전이며 C++ ABI나 package 버전 요구사항이 아니다. `random.hpp`의 `random_within_range`는 `detail/random.hpp`에 선언된 난수 API를 공개한다. fixture와 backtrace는 각각 `RAFTPP_WITH_FIXTURE`, `RAFTPP_WITH_BACKTRACE`로 선택하며 UV와 독립이다.

## 난수 보조 함수

```cpp
uint32_t random_within_range(uint32_t& state, uint32_t minimum,
                           uint32_t maximum) noexcept;
```

`<raftpp/random.hpp>`에서 제공한다. 호출자가 소유한 `state`를 갱신하며 양 끝을 포함한 범위의 값을 반환한다. `minimum <= maximum`이 사전조건이다. 같은 시드는 같은 순서를 재현하며 단일 값 범위는 그 값을 반환하고 state를 진행시키지 않는다. 암호화용 난수 API가 아니며 state의 동시 접근은 호출자가 직렬화한다.

`storage.hpp`, `timer.hpp`, `composed_io.hpp`는 사용자가 선택한 저장소·타이머·transport·스냅샷 코덱으로 `shared<io>`를 만드는 `make_io(io_components)`를 제공한다. 생성 검증·옵션·초기화/종료·형식 계약은 [I/O 조립](../composition.md)을 따른다.
